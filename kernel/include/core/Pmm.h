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
                char list[sizeof(sl::ListHook)]; //see Heap.cpp for the definition of this field, this is done to avoid header pollution (Pmm.h is included in a lot of places)
                uint16_t used;
            } slab;

            sl::FwdListHook vmObjList; //linkage for VmObj page list
            struct
            {
                char placeholder[sizeof(sl::FwdListHook)];
                uint16_t offset; //in pages (not bytes) of this page within the VmObject
                uint16_t pinCount;
            } vm;
        };
    };

    class Pmm
    {
    private:
        struct PmList
        {
            sl::RunLevelLock<RunLevel::Dpc> lock;
            sl::FwdList<PageInfo, &PageInfo::mmList> list;
            size_t size;
        };

        PmList globalList;

        bool trashAfterUse;
        bool trashBeforeUse;
        PageInfo* infoDb;

        void IngestMemory(sl::Span<MemmapEntry> entries);
        sl::Opt<uintptr_t> AllocFromList(PmList& list);

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
