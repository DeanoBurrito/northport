#include <NamespacePrivate.hpp>
#include <Maths.hpp>

namespace Npk
{
    constexpr size_t InitialTableEntries = 16;

    struct HandleTable
    {
        Mutex mutex;
        sl::Span<Handle> entries;
        size_t firstFreeEntry;
    };

    static size_t GetExpandedHandleTableEntryCount(size_t entries)
    {
        return entries + (entries / 2);
    }

    NsStatus CreateHandleTableSized(HandleTable** table, size_t entryCount)
    {
        using Private::HandleHeapTag;

        entryCount = sl::AlignUp(entryCount, InitialTableEntries);

        void* ptr = PoolAllocPaged(sizeof(HandleTable), HandleHeapTag);
        if (ptr == nullptr)
            return NsStatus::Shortage;

        void* entries = PoolAllocPaged(sizeof(Handle) * entryCount, 
            HandleHeapTag);
        if (entries == nullptr)
        {
            PoolFreePaged(ptr, sizeof(HandleTable), HandleHeapTag);
            return NsStatus::Shortage;
        }

        auto* latest = new(ptr) HandleTable {};

        if (!ResetMutex(&latest->mutex, 1))
        {
            PoolFreePaged(entries, sizeof(Handle) * entryCount, 
                HandleHeapTag);
            PoolFreePaged(ptr, sizeof(HandleTable), HandleHeapTag);
            return NsStatus::InternalError;
        }

        Handle* entryArray = static_cast<Handle*>(entries);
        latest->entries = sl::Span<Handle>(entryArray, entryCount);
        latest->firstFreeEntry = 0;

        *table = latest;
        return NsStatus::Success;
    }

    NsStatus CreateHandleTable(HandleTable** table)
    {
        return CreateHandleTableSized(table, InitialTableEntries);
    }

    bool DestroyHandleTable(HandleTable& table)
    {
        using Private::HandleHeapTag;

        if (!ResetMutex(&table.mutex, 0))
            return false;

        size_t leakedCount = 0;
        for (size_t i = 0; i < table.entries.Size(); i++)
        {
            if (table.entries[i] == InvalidHandle)
                continue;

            UnrefObject(*table.entries[i]);
            leakedCount++;
        }

        if (leakedCount != 0)
        {
            Log("Handle table %p had %zu open handles at destruction.",
                LogLevel::Warning, &table, leakedCount);
        }

        if (!PoolFreePaged(table.entries.Begin(), table.entries.SizeBytes(),
            HandleHeapTag))
        {
            return false;
        }
        
        if (!PoolFreePaged(&table, sizeof(table), HandleHeapTag))
            return false;

        return true;
    }

    NsStatus DuplicateHandleTable(HandleTable** copy, HandleTable& source)
    {
        HandleTable* other = nullptr;

        auto result = CreateHandleTableSized(&other, source.entries.Size());
        if (result != NsStatus::Success)
            return result;

        if (!AcquireMutex(&source.mutex, sl::NoTimeout, NPK_WAIT_LOCATION))
        {
            DestroyHandleTable(*other);
            return NsStatus::InternalError;
        }

        for (size_t i = 0; i < source.entries.Size(); i++)
        {
            if (source.entries[i] == InvalidHandle)
                continue;

            //I dont like having an assert here (fatal failure path), but I'll
            //justify it by saying a handle existing requires the object to
            //have a non-zero refcount. If `RefObject()` fails here that means
            //someone has called `UnrefObject()` twice elsewhere, so we have a
            //bug.
            NPK_ASSERT(RefObject(*source.entries[i]));

            other->entries[i] = source.entries[i];
        }

        other->firstFreeEntry = source.firstFreeEntry;

        sl::AtomicThreadFence(sl::Release);
        *copy = other;

        return NsStatus::Success;
    }

    NsStatus CreateHandle(Handle* handle, HandleTable& table, NsObject& obj)
    {
        if (!AcquireMutex(&table.mutex, sl::NoTimeout, NPK_WAIT_LOCATION))
            return NsStatus::InternalError;

        if (!RefObject(obj))
        {
            ReleaseMutex(&table.mutex);
            return NsStatus::BadObject;
        }

        //the object is valid and we've got the table's mutex, find or create
        //a valid slot.
        if (table.firstFreeEntry == table.entries.Size())
        {
            const size_t allocSize = sizeof(Handle) * 
                GetExpandedHandleTableEntryCount(table.entries.Size());

            void* latest = PoolAllocPaged(allocSize, Private::HandleHeapTag);
            if (latest == nullptr)
            {
                ReleaseMutex(&table.mutex);
                UnrefObject(obj);
                
                return NsStatus::Shortage;
            }

            Handle* newEntries = static_cast<Handle*>(latest);
            for (size_t i = 0; i < table.entries.Size(); i++)
                newEntries[i] = table.entries[i];

            void* prevEntries = table.entries.Begin();
            const size_t prevEntriesSize = table.entries.SizeBytes();
            table.entries = { newEntries, allocSize / sizeof(Handle) };
            PoolFreePaged(prevEntries, prevEntriesSize, Private::HandleHeapTag);
        }
        
        NPK_ASSERT(table.entries[table.firstFreeEntry] == nullptr);

        table.entries[table.firstFreeEntry] = &obj;
        *handle = table.entries[table.firstFreeEntry];

        while (table.entries[table.firstFreeEntry] != InvalidHandle
            && table.firstFreeEntry < table.entries.Size())
        {
            table.firstFreeEntry++;
        }

        ReleaseMutex(&table.mutex);

        return NsStatus::Success;
    }

    bool DestroyHandle(Handle& handle, HandleTable& table)
    {
        if (!AcquireMutex(&table.mutex, sl::NoTimeout, NPK_WAIT_LOCATION))
            return false;

        const size_t index = &handle - table.entries.Begin();

        bool success = false;
        if (index < table.entries.Size() 
            && table.entries[index] != InvalidHandle)
        {
            Handle obj = table.entries[index];
            table.entries[index] = InvalidHandle;
            table.firstFreeEntry = sl::Min(table.firstFreeEntry, index);

            UnrefObject(*obj);
            success = true;
        }

        ReleaseMutex(&table.mutex);
        return success;
    }

    bool GetHandleValue(NsObject** object, Handle& handle, HandleTable& table)
    {
        if (!AcquireMutex(&table.mutex, sl::NoTimeout, NPK_WAIT_LOCATION))
            return false;

        const bool result = GetHandleValueLocked(object, handle, table);
        ReleaseMutex(&table.mutex);

        return result;
    }

    bool GetHandleValueLocked(NsObject** object, Handle& handle,
        HandleTable& table)
    {
        const size_t index = &handle - table.entries.Begin();

        bool success = false;
        if (index < table.entries.Size() && table.entries[index] != nullptr)
        {
            *object = table.entries[index];
            RefObject(**object);
            success = true;
        }

        return success;
    }
}
