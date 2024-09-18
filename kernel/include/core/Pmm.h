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
        union 
        {
            uint16_t pmCount; //count of contiguous pages
            uint16_t objOffset; //offset (in pages, not bytes) of this page within the VmObject
        };
        //uint32_t unused here
        sl::FwdListHook mmList; //used by pmm (when page is free), or vmm (page is used)
        sl::FwdListHook objList; //used by vmo using this page
    };

    class Pmm
    {
    private:
        sl::RunLevelLock<RunLevel::Dpc> listLock;
        sl::FwdList<PageInfo, &PageInfo::mmList> list;
        size_t listSize;

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
