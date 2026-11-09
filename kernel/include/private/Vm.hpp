#pragma once

#include "../Vm.hpp"
#include <Core.hpp>
#include <Hardware.hpp>

namespace Npk::Private
{
    constexpr size_t TrieIndexBits = 32;
    constexpr size_t TrieNodeAlign = 16;
    constexpr uintptr_t TrieHeightMask = TrieNodeAlign - 1;

    enum class ZoneSharing
    {
        Packed,
        CpuShared,
    };

    struct TrieSpares
    {
        static constexpr size_t Capacity = 16;

        void* nodes[Capacity];
        size_t count;
    };

    constexpr size_t TrieLog2(size_t value)
    {
        size_t bits = 0;
        while ((static_cast<size_t>(1) << bits) < value)
            bits++;

        return bits;
    }

    void RegisterExactPoolZone(HeapTag tag, size_t size, ZoneSharing sharing);

    void* AllocTrieNode(size_t size);
    void FreeTrieNode(void* node, size_t size);
    void RetireTrieNode(void* node, EbrCallback freeNow);

    //TODO: fully template and move to syslib
    template<typename Leaf, typename Traits, size_t Budget>
    struct Trie
    {
        constexpr static size_t NodeSize = sizeof(EbrItem) + Budget;
        constexpr static size_t LeafFanout = Budget / sizeof(Leaf);
        constexpr static size_t LeafBits = TrieLog2(LeafFanout);
        constexpr static size_t InteriorFanout = Budget / sizeof(void*);
        constexpr static size_t InteriorBits = TrieLog2(InteriorFanout);
        constexpr static size_t MaxHeight = 1 + (TrieIndexBits - LeafBits
            + InteriorBits - 1) / InteriorBits;

        struct LeafNode
        {
            EbrItem retire;
            Leaf slots[LeafFanout];
        };

        struct InteriorNode
        {
            EbrItem retire;
            sl::Atomic<void*> children[InteriorFanout];
        };

        static_assert((static_cast<size_t>(1) << LeafBits) == LeafFanout);
        static_assert((static_cast<size_t>(1) << InteriorBits)
            == InteriorFanout);
        static_assert(MaxHeight <= TrieHeightMask);
        static_assert(2 * MaxHeight <= TrieSpares::Capacity);
        static_assert(sizeof(LeafNode) <= NodeSize);
        static_assert(sizeof(InteriorNode) <= NodeSize);
        static_assert(offsetof(LeafNode, retire) == 0);
        static_assert(offsetof(InteriorNode, retire) == 0);

        static void FreeRetired(EbrItem* item)
        {
            FreeTrieNode(item, NodeSize);
        }

        static void Retire(void* node)
        {
            RetireTrieNode(node, FreeRetired);
        }

        static NpkStatus FillSpares(TrieSpares& spares, size_t want)
        {
            NPK_ASSERT(want <= TrieSpares::Capacity);

            while (spares.count < want)
            {
                void* node = AllocTrieNode(NodeSize);
                if (node == nullptr)
                    return NpkStatus::Shortage;

                spares.nodes[spares.count++] = node;
            }

            return NpkStatus::Success;
        }

        static void DrainSpares(TrieSpares& spares)
        {
            while (spares.count != 0)
                FreeTrieNode(spares.nodes[--spares.count], NodeSize);
        }

        static size_t HeightOf(uintptr_t word)
        {
            return word & TrieHeightMask;
        }

        static void* NodeOf(uintptr_t word)
        {
            return reinterpret_cast<void*>(word & ~TrieHeightMask);
        }

        static uintptr_t Pack(void* node, size_t height)
        {
            const auto value = reinterpret_cast<uintptr_t>(node);
            NPK_ASSERT((value & TrieHeightMask) == 0);

            return value | height;
        }

        static size_t BitsAt(size_t height)
        {
            if (height == 0)
                return 0;

            return LeafBits + (height - 1) * InteriorBits;
        }

        static bool Fits(size_t height, size_t index)
        {
            const size_t bits = BitsAt(height);
            if (bits >= sizeof(size_t) * 8)
                return true;

            return (index >> bits) == 0;
        }

        static size_t HeightFor(size_t index)
        {
            size_t height = 1;
            while (!Fits(height, index))
                height++;

            return height;
        }

        static size_t ChildIndex(size_t index, size_t level)
        {
            if (level == 1)
                return index & (LeafFanout - 1);

            const size_t shift = BitsAt(level - 1);

            return (index >> shift) & (InteriorFanout - 1);
        }

        static Leaf* Lookup(const sl::Atomic<uintptr_t>& root, size_t index,
            size_t* run = nullptr)
        {
            const uintptr_t word = root.Load(sl::Acquire);

            size_t level = HeightOf(word);
            if (level == 0 || !Fits(level, index))
                return nullptr;

            void* node = NodeOf(word);
            while (level > 1)
            {
                auto* interior = static_cast<InteriorNode*>(node);
                const size_t child = ChildIndex(index, level);
                node = interior->children[child].Load(sl::Acquire);

                if (node == nullptr)
                    return nullptr;
                level--;
            }

            auto* leaf = static_cast<LeafNode*>(node);
            const size_t slot = ChildIndex(index, 1);
            if (run != nullptr)
                *run = LeafFanout - slot;

            return &leaf->slots[slot];
        }

        static size_t NodesNeeded(const sl::Atomic<uintptr_t>& root,
            size_t index)
        {
            const uintptr_t word = root.Load(sl::Acquire);
            size_t height = HeightOf(word);
            const size_t target = HeightFor(index);

            if (height == 0)
                return target;
            if (height < target)
                return (target - height) + (target - 1);

            size_t needed = 0;
            size_t level = height;
            void* node = NodeOf(word);
            while (level > 1)
            {
                auto* interior = static_cast<InteriorNode*>(node);
                const size_t child = ChildIndex(index, level);
                node = interior->children[child].Load(sl::Acquire);

                level--;
                if (node == nullptr)
                {
                    needed = level;
                    break;
                }
            }

            return needed;
        }

        static Leaf* Ensure(sl::Atomic<uintptr_t>& root, size_t index,
            TrieSpares& spares)
        {
            if (spares.count < NodesNeeded(root, index))
                return nullptr;

            uintptr_t word = root.Load(sl::Acquire);
            size_t height = HeightOf(word);
            const size_t target = HeightFor(index);

            if (height == 0)
            {
                void* node = spares.nodes[--spares.count];
                height = target;
                word = Pack(node, height);

                root.Store(word, sl::Release);
            }

            while (height < target)
            {
                auto* top = static_cast<InteriorNode*>(
                    spares.nodes[--spares.count]);

                top->children[0].Store(NodeOf(word), sl::Relaxed);
                height++;
                word = Pack(top, height);

                root.Store(word, sl::Release);
            }

            size_t level = height;
            void* node = NodeOf(word);
            while (level > 1)
            {
                auto* interior = static_cast<InteriorNode*>(node);

                const size_t child = ChildIndex(index, level);
                void* next = interior->children[child].Load(sl::Acquire);
                if (next == nullptr)
                {
                    next = spares.nodes[--spares.count];
                    interior->children[child].Store(next, sl::Release);
                }

                node = next;
                level--;
            }

            auto* leaf = static_cast<LeafNode*>(node);

            return &leaf->slots[ChildIndex(index, 1)];
        }

        static bool LeafVacant(const LeafNode* leaf)
        {
            for (size_t i = 0; i < LeafFanout; i++)
            {
                if (!Traits::Vacant(leaf->slots[i]))
                    return false;
            }

            return true;
        }

        static bool InteriorVacant(const InteriorNode* node)
        {
            for (size_t i = 0; i < InteriorFanout; i++)
            {
                if (node->children[i].Load(sl::Relaxed) != nullptr)
                    return false;
            }

            return true;
        }

        static void Prune(sl::Atomic<uintptr_t>& root, size_t index)
        {
            uintptr_t word = root.Load(sl::Acquire);
            const size_t height = HeightOf(word);
            if (height == 0 || !Fits(height, index))
                return;

            InteriorNode* path[MaxHeight];

            void* node = NodeOf(word);
            size_t level = height;
            while (level > 1)
            {
                auto* interior = static_cast<InteriorNode*>(node);

                path[level - 1] = interior;
                const size_t child = ChildIndex(index, level);
                node = interior->children[child].Load(sl::Acquire);

                if (node == nullptr)
                    break;
                level--;
            }

            if (level == 1)
            {
                if (!LeafVacant(static_cast<LeafNode*>(node)))
                {
                    Shrink(root);

                    return;
                }

                UnlinkNode(root, path, height, 1, index, node);
                level = 2;
            }

            for (; level <= height; level++)
            {
                InteriorNode* interior = path[level - 1];
                if (!InteriorVacant(interior))
                    break;
                UnlinkNode(root, path, height, level, index, interior);
            }

            Shrink(root);
        }

        static void UnlinkNode(sl::Atomic<uintptr_t>& root,
            InteriorNode** path, size_t height, size_t level, size_t index,
            void* node)
        {
            if (level == height)
                root.Store(0, sl::Release);
            else
            {
                InteriorNode* parent = path[level];
                const size_t child = ChildIndex(index, level + 1);

                parent->children[child].Store(nullptr, sl::Release);
            }

            Retire(node);
        }

        static void Shrink(sl::Atomic<uintptr_t>& root)
        {
            while (true)
            {
                const uintptr_t word = root.Load(sl::Acquire);
                const size_t height = HeightOf(word);
                if (height < 2)
                    return;

                auto* top = static_cast<InteriorNode*>(NodeOf(word));
                void* first = top->children[0].Load(sl::Acquire);
                if (first == nullptr)
                    return;

                for (size_t i = 1; i < InteriorFanout; i++)
                {
                    if (top->children[i].Load(sl::Relaxed) != nullptr)
                        return;
                }

                root.Store(Pack(first, height - 1), sl::Release);
                Retire(top);
            }
        }

        static Leaf* Next(size_t& index, const sl::Atomic<uintptr_t>& root,
            size_t from)
        {
            const uintptr_t word = root.Load(sl::Acquire);
            const size_t height = HeightOf(word);
            if (height == 0)
                return nullptr;

            size_t cursor = from;
            while (Fits(height, cursor))
            {
                void* node = NodeOf(word);
                size_t level = height;
                bool skipped = false;

                while (level > 1)
                {
                    auto* interior = static_cast<InteriorNode*>(node);
                    const size_t child = ChildIndex(cursor, level);
                    node = interior->children[child].Load(sl::Acquire);

                    if (node == nullptr)
                    {
                        const size_t span = static_cast<size_t>(1)
                            << BitsAt(level - 1);
                        cursor = (cursor & ~(span - 1)) + span;
                        skipped = true;
                        break;
                    }

                    level--;
                }
                if (skipped)
                {
                    if (cursor == 0)
                        return nullptr;
                    continue;
                }

                auto* leaf = static_cast<LeafNode*>(node);
                for (size_t i = ChildIndex(cursor, 1); i < LeafFanout; i++)
                {
                    if (Traits::Vacant(leaf->slots[i]))
                        continue;
                    index = (cursor & ~(LeafFanout - 1)) + i;

                    return &leaf->slots[i];
                }

                cursor = (cursor & ~(LeafFanout - 1)) + LeafFanout;
                if (cursor == 0)
                    return nullptr;
            }

            return nullptr;
        }

        static void Destroy(sl::Atomic<uintptr_t>& root)
        {
            const uintptr_t word = root.Load(sl::Acquire);
            root.Store(0, sl::Release);
            if (HeightOf(word) == 0)
                return;

            DestroyNode(NodeOf(word), HeightOf(word));
        }

        static void DestroyNode(void* node, size_t level)
        {
            if (level > 1)
            {
                auto* interior = static_cast<InteriorNode*>(node);

                for (size_t i = 0; i < InteriorFanout; i++)
                {
                    void* child = interior->children[i].Load(sl::Relaxed);
                    if (child != nullptr)
                        DestroyNode(child, level - 1);
                }
            }
            else
            {
                const auto result = LeafVacant(static_cast<LeafNode*>(node));
                NPK_ASSERT(result);
            }

            Retire(node);
        }
    };
}

namespace Npk
{
    /* Forward declaration for `NamedSourceAttach()`.
     */
    struct NsObject;

    NpkStatus SpaceLookupLocked(VmRange** found, VmSpace& space, uintptr_t addr,
        size_t length);

    void PinPage(PageInfo* page);
    void UnpinPage(PageInfo* page, size_t count = 1);
}

namespace Npk::Private
{
    constexpr auto VmSourceTag = NPK_MAKE_HEAP_TAG("VSrc");
    constexpr HeapTag TrieNodeTag = NPK_MAKE_HEAP_TAG("Trie");

    enum class TrimPressure
    {
        None,
        Reclaim,
        LowLevel,
        MinLevel,
    };

    LiveListHead& GetLiveListHead(SystemDomain& dom, LiveList which);
    //NOTE: assumes `dom.liveLists.lock` is held
    void LiveListLink(SystemDomain& dom, LiveList which, PageInfo* page);
    //NOTE: assumes `dom.liveLists.lock` is held
    void LiveListUnlink(SystemDomain& dom, PageInfo* page);

    SL_ALWAYS_INLINE
    LiveList IdToLiveList(uint8_t num)
    {
        return static_cast<LiveList>(num - 1);
    }

    SL_ALWAYS_INLINE
    uint8_t LiveListToId(LiveList list)
    {
        return static_cast<uint8_t>((size_t)list + 1);
    }

    NpkStatus CreateAnonPage(AnonPage** page);
    void DestroyAnonPage(AnonPage* page);
    NpkStatus AnonPageGetPage(PageInfo** info, AnonPageRef page);

    NpkStatus CreateAnonMap(AnonMap** map, size_t slotCount);
    void DestroyAnonMap(AnonMap* map);
    NpkStatus ResizeAnonMap(AnonMap& map, size_t newSlotCount);
    AnonPageRef AnonMapLookup(AnonMap& map, size_t slot);
    AnonPageRef AnonMapLookupLocked(AnonMap& map, size_t slot);

    /* Links a range to an anon map, the range mutex may or may not held, but
     * the `map.mutex` must not be. Same applies to UnlinkRange().
     */
    NpkStatus AnonMapLinkRange(AnonMap& map, VmRange* range);
    NpkStatus AnonMapUnlinkRange(AnonMap& map, VmRange* range);

    NpkStatus AnonMapAdd(AnonMap& map, size_t slot, AnonPageRef& anon);
    AnonPageRef AnonMapRemove(AnonMap& map, size_t slot);
    NpkStatus AnonMapClone(AnonMapRef* clone, AnonMap& source);

    VmSource* AnonSourceAttach(size_t size);
    VmSource* NamedSourceAttach(NsObject& obj);
    VmSource* ModuleSourceAttach();
    //VmSource* DeviceSourceAttach(); TODO: revisit after driver subsystem

    bool ModuleSourceAvailable();
    NpkStatus InitModuleSource(uint64_t seed, size_t failPercent,
        size_t maxDelayUs);
    NpkStatus InitStackCaches();

    /* Backend of `FreeKenrelStack()`, `keepPerCpu` is the float count.
     * Returns number of stacks freed.
     */
    size_t TrimKernelStackCaches(size_t keepPerCpu, sl::TimeCount timeout);

    void PinPage(PageInfo* page);
    void UnpinPage(PageInfo* page, size_t count = 1);
    void TrackPage(PageInfo* page); //places page on active list
    void UntrackPage(PageInfo* page); //removes page from a live list (if any)
    void DrainSourcePages(VmSource& src);

    void ClearPageMapped(PageInfo* page);
    void BeginMappingRemovals(VmRange& range);
    void EndMappingRemovals(VmRange& range);
    void SettleAnonPage(AnonMap& map, PageInfo* page, bool dirtied,
        bool unpin);
    void SettleSourcePage(VmSource& src, PageInfo* page, bool dirtied,
        bool unpin);
    NpkStatus SettleMappedPage(Paddr paddr, bool dirtied);

    /* Runs once pass of page trimming, exposed here for testing purposes only.
     */
    size_t TrimOnce(size_t batch, bool ignoreRefs = false,
        bool exactRefs = false, TrimPressure pressure = TrimPressure::Reclaim);
}
