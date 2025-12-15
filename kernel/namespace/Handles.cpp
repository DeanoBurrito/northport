#include <NamespacePrivate.hpp>

namespace Npk
{
    constexpr size_t InitialTableEntries = 16;

    struct HandleTable
    {
        Waitable mutex;
        sl::Span<Handle> entries;
    };

    NsStatus CreateHandleTable(HandleTable** table)
    {
        using Private::HandleHeapTag;

        void* ptr = PoolAllocPaged(sizeof(HandleTable), HandleHeapTag);
        if (ptr == nullptr)
            return NsStatus::Shortage;

        void* entries = PoolAllocPaged(sizeof(Handle) * InitialTableEntries,
            HandleHeapTag);
        if (entries == nullptr)
        {
            PoolFreePaged(ptr, sizeof(HandleTable), HandleHeapTag);
            return NsStatus::Shortage;
        }

        auto* latest = new(ptr) HandleTable {};

        if (!ResetWaitable(&latest->mutex, WaitableType::Mutex, 1))
        {
            PoolFreePaged(entries, sizeof(Handle) * InitialTableEntries, 
                HandleHeapTag);
            PoolFreePaged(ptr, sizeof(HandleTable), HandleHeapTag);
            return NsStatus::InternalError;
        }

        Handle* entryArray = static_cast<Handle*>(entries);
        latest->entries = sl::Span<Handle>(entryArray, InitialTableEntries);

        *table = latest;
        return NsStatus::Success;
    }

    bool DestroyHandleTable(HandleTable& table)
    {
        using Private::HandleHeapTag;

        if (!ResetWaitable(&table.mutex, WaitableType::Mutex, 0))
            return false;

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

        auto result = CreateHandleTable(&other);
        if (result != NsStatus::Success)
            return result;

        //TODO: copy contents

        *copy = other;
        return NsStatus::Success;
    }

    NsStatus CreateHandle(Handle* handle, HandleTable& table, NsObject& obj)
    { NPK_UNREACHABLE(); }

    bool DestroyHandle(Handle& handle, HandleTable& table)
    { NPK_UNREACHABLE(); }

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

        if (index < table.entries.Size() && table.entries[index] != nullptr)
        {
            *object = table.entries[index];
            RefObject(**object);
            return true;
        }

        return false;
    }
}
