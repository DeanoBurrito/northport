#pragma once

#include <Locks.h>
#include <containers/List.h>
#include <Optional.h>
#include <interfaces/intra/Compiler.h>

namespace Npk { struct MemmapEntry; } //defined in interfaces/loader/Generic.h

namespace Npk::Core
{
    struct PageInfo
    {
        sl::FwdListHook mmList; //used whichever memory subsystem owns this page
        union
        {
            struct
            {
                uint16_t count; //number of contiguous pages in this chunk
            } pm;

            struct
            {
                void* list[2]; //avoiding header pollution, see Heap.cpp for details
                uint16_t used;
            } slab;

            struct
            {
                sl::FwdListHook objList; //linkage for VmObject page-list
                uint16_t offset; //in pages (not bytes) of this page within the VmObject
                uint16_t pinCount;
            } vm;
        };
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

        inline PageInfo* Lookup(uintptr_t paddr)
        {
            return infoDb + (paddr >> PfnShift());
        }

        sl::Opt<uintptr_t> Alloc();
        void Free(uintptr_t paddr);
    };

    ALWAYS_INLINE
    PageInfo* PmLookup(uintptr_t paddr) 
    { return Pmm::Global().Lookup(paddr); }

    ALWAYS_INLINE
    sl::Opt<uintptr_t> PmAlloc()
    { return Pmm::Global().Alloc(); }

    ALWAYS_INLINE
    void PmFree(uintptr_t paddr)
    { Pmm::Global().Free(paddr); }
}
