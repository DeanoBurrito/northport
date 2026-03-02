#include <VmPrivate.hpp>

namespace Npk::Private
{
    constexpr auto AmapTag = NPK_MAKE_HEAP_TAG("Amap");
    constexpr auto AnonPageTag = NPK_MAKE_HEAP_TAG("AnPg");
    constexpr size_t TableEntryBits = 6;
    constexpr size_t TableEntryCount = 1 << TableEntryBits;

    struct AnonTable
    {
        void* entries[TableEntryCount];
        uint8_t validCount;
    };
    static_assert(sizeof(void*) >= sizeof(AnonPageRef));

    constexpr size_t AnonTableLevels(size_t slotCount)
    {
        size_t levels = 0;
        while (slotCount != 0)
        {
            slotCount >>= TableEntryBits;
            levels++;
        }

        return levels;
    }

    constexpr size_t AnonTableIndex(size_t level, size_t slot)
    {
        const size_t shift = level * TableEntryBits;
        slot >>= shift;
        slot &= TableEntryCount - 1;

        return slot;
    }

    static AnonTable* AllocAnonTable()
    {
        void* ptr = PoolAllocWired(sizeof(AnonTable), AmapTag);
        if (ptr == nullptr)
            return nullptr;

        return new(ptr) AnonTable {};
    }

    static void FreeAnonTable(AnonTable* table, size_t level)
    {
        NPK_ASSERT(table != nullptr);

        for (size_t i = 0; i < TableEntryCount; i++)
        {
            if (level == 0)
            {
                AnonPageRef ref = sl::Move(
                    *reinterpret_cast<AnonPageRef*>(&table->entries[i]));

                //`ref` goes out of scope here, the RAII type will cause the
                //refcount of the anon page to be decremented.
                continue;
            }

            AnonTable* ref = static_cast<AnonTable*>(table->entries[i]);
            if (ref != nullptr)
                FreeAnonTable(ref, level - 1);
        }

        PoolFreeWired(table, sizeof(*table), AmapTag);
    }

    VmStatus CreateAnonPage(AnonPage** page)
    {
        if (page == nullptr)
            return VmStatus::InvalidArg;

        void* ptr = PoolAllocWired(sizeof(AnonPage), AnonPageTag);
        if (ptr == nullptr)
            return VmStatus::Shortage;

        auto* newPage = new(ptr) AnonPage {};
        *page = newPage;

        return VmStatus::Success;
    }

    void DestroyAnonPage(AnonPage* page)
    {
        NPK_ASSERT(page != nullptr);
        NPK_ASSERT(page->refcount == 0);

        //ensure we're not leaking any resources (these pointers should be
        //null if there's nothing attached).
        NPK_ASSERT(page->swapSlot == nullptr);
        NPK_ASSERT(page->page == nullptr);

        PoolFreeWired(page, sizeof(*page), AnonPageTag);
    }

    VmStatus CreateAnonMap(AnonMap** map, size_t slotCount)
    {
        if (map == nullptr)
            return VmStatus::InvalidArg;

        void* ptr = PoolAllocWired(sizeof(AnonMap), AmapTag);
        if (ptr == nullptr)
            return VmStatus::Shortage;
        auto* newMap = new(ptr) AnonMap {};

        if (slotCount > 0)
        {
            auto status = ResizeAnonMap(*newMap, slotCount);

            if (status != VmStatus::Success)
            {
                PoolFreeWired(newMap, sizeof(*newMap), AmapTag);
                return status;
            }
        }

        *map = newMap;
        return VmStatus::Success;
    }

    void DestroyAnonMap(AnonMap* map)
    {
        NPK_ASSERT(map != nullptr);
        NPK_ASSERT(map->refcount == 0);

        if (map->slotCount != 0)
        {
            auto table = static_cast<AnonTable*>(map->slots);
            const size_t levels = AnonTableLevels(map->slotCount);
            FreeAnonTable(table, levels - 1);
        }

        PoolFreeWired(map, sizeof(*map), AmapTag);
    }

    VmStatus ResizeAnonMap(AnonMap& map, size_t newSlotCount)
    {
        AnonMapRef mapRef = &map;

        if (!AcquireMutex(&map.mutex, sl::NoTimeout))
            return VmStatus::InternalError;

        if (newSlotCount <= map.slotCount)
        {
            ReleaseMutex(&map.mutex);
            return VmStatus::InvalidArg;
        }

        const size_t curLevels = AnonTableLevels(map.slotCount);
        const size_t newLevels = AnonTableLevels(newSlotCount);
        if (newLevels == curLevels)
        {
            //in this case we dont actually need to do anything, as the
            //number of levels stays the same. Return success.
            ReleaseMutex(&map.mutex);
            return VmStatus::Success;
        }

        if (map.slots != nullptr)
        {
            //there is already a table structure allocated, create a new 
            //set of tables to point to the existing mappings.
            auto* originalRoot = static_cast<AnonTable*>(map.slots);

            for (size_t i = curLevels; i != newLevels; i++)
            {
                auto* table = AllocAnonTable();
                if (table == nullptr)
                {
                    for (size_t j = 0; j < newLevels - curLevels; j++)
                    {
                        auto* root = static_cast<AnonTable*>(map.slots);
                        map.slots = root->entries[0];

                        FreeAnonTable(root, curLevels + j);
                    }
                    map.slots = originalRoot;

                    ReleaseMutex(&map.mutex);
                    return VmStatus::Shortage;
                }

                auto* curRoot = static_cast<AnonTable*>(map.slots);
                table->entries[0] = curRoot;
                map.slots = table;
            }
        }
        //else: no existing tables, we'll do it lazily.

        ReleaseMutex(&map.mutex);
        
        return VmStatus::Success;
    }

    AnonPageRef AnonMapLookup(AnonMap& map, size_t slot)
    {
        AnonMapRef mapRef = &map;

        if (!AcquireMutex(&map.mutex, sl::NoTimeout))
            return {};

        AnonPageRef ref {};

        const size_t levels = AnonTableLevels(map.slotCount);
        auto* table = static_cast<AnonTable*>(map.slots);
        for (size_t i = levels; table != nullptr && i != 0; i--)
        {
            const size_t level = i - 1;
            const size_t index = AnonTableIndex(level, slot);

            auto* entry = table->entries[index];
            if (entry == nullptr)
                break;

            if (level == 0)
                ref = *reinterpret_cast<AnonPageRef*>(&entry);
            else
                table = static_cast<AnonTable*>(entry);
        }

        ReleaseMutex(&map.mutex);

        return ref;
    }

    void AnonMapAdd(AnonMap& map, size_t slot, AnonPageRef& anon)
    {
        AnonMapRef mapRef = &map;

        if (!AcquireMutex(&map.mutex, sl::NoTimeout))
            return;

        if (slot >= map.slotCount)
        {
            ReleaseMutex(&map.mutex);
            return;
        }

        if (map.slots == nullptr)
        {
            map.slots = AllocAnonTable();
            NPK_ASSERT(map.slots != nullptr); //TODO: non-fatal handling
        }

        const size_t levels = AnonTableLevels(map.slotCount);
        auto* table = static_cast<AnonTable*>(map.slots);
        for (size_t i = levels; i != 0; i--)
        {
            const size_t level = i - 1;
            const size_t index = AnonTableIndex(level, slot);

            auto* entry = table->entries[index];
            if (entry == nullptr)
            {
                auto newTable = AllocAnonTable();
                NPK_ASSERT(newTable != nullptr);
                table->entries[index] = newTable;

                entry = newTable;
            }

            if (level == 0)
            {
                auto ref = *reinterpret_cast<AnonPageRef*>(&entry);
                ref = anon;
            }
            else
                table = static_cast<AnonTable*>(entry);
        }

        ReleaseMutex(&map.mutex);
    }

    AnonPageRef AnonMapRemove(AnonMap& map, size_t slot)
    {
        AnonMapRef mapRef = &map;

        if (!AcquireMutex(&map.mutex, sl::NoTimeout))
            return {};

        if (map.slots == nullptr || slot >= map.slotCount)
        {
            ReleaseMutex(&map.mutex);
            return {};
        }

        AnonPageRef ref {};
        const size_t levels = AnonTableLevels(map.slotCount);
        auto* table = static_cast<AnonTable*>(map.slots);
        for (size_t i = levels; i != 0; i--)
        {
            const size_t level = i - 1;
            const size_t index = AnonTableIndex(level, slot);

            auto* entry = table->entries[index];
            if (entry == nullptr)
                break;

            if (level == 0)
            {
                ref = sl::Move(*reinterpret_cast<AnonPageRef*>(&entry));
                entry = nullptr;

                //TODO: freeing of empty tables
            }
            else
                table = static_cast<AnonTable*>(entry);
        }

        ReleaseMutex(&map.mutex);

        return ref;
    }
}
