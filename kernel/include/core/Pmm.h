#pragma once

#include <Locks.h>
#include <containers/List.h>
#include <Optional.h>

namespace Npk { struct MemmapEntry; } //defined in interfaces/loader/Generic.h

namespace Npk::Core
{
    enum PmFlags : uint16_t
    {
        None = 0,
        Busy = 1 << 1,
        Dirty = 1 << 2,
    };

    struct PageInfo
    {
        PmFlags flags;
        uint16_t objOffset; //pages, not bytes
        //uint32_t unused here
        sl::FwdListHook vmList;
        sl::FwdListHook objList;
    };

    struct PmFreeEntry
    {
        PmFreeEntry* next;
        size_t count;
    };

    class Pmm
    {
    private:
        struct
        {
            sl::RunLevelLock<RunLevel::Dpc> lock;
            PmFreeEntry* head;
            size_t size;
        } freelist;

        bool trashAfterUse;
        bool trashBeforeUse;
        PageInfo* infoDb;

        void IngestMemory(sl::Span<MemmapEntry> entries);

    public:
        static Pmm& Global();

        void Init();
        void InitLocalCache();
        void ReclaimLoaderMemory();
        PageInfo* Lookup(uintptr_t paddr);

        sl::Opt<uintptr_t> Alloc();
        void Free(uintptr_t paddr);
    };

    static inline sl::Opt<uintptr_t> PmAlloc()
    { return Pmm::Global().Alloc(); }

    static inline void PmFree(uintptr_t paddr)
    { Pmm::Global().Free(paddr); }
}
