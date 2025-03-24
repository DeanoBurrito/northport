#pragma once

#include <core/Event.h>
#include <containers/List.h>
#include <Types.h>

namespace Npk
{
    struct MmuSpace;

    struct FreeSlab
    {
        sl::FwdListHook next;
    };

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
                sl::FwdList<FreeSlab, &FreeSlab::next> list;
                uint16_t used;
            } slab;

            sl::FwdListHook vmObjList; //linkage for VmObj page list
            struct
            {
                char placeholder[sizeof(sl::FwdListHook)];
                uint32_t flags;
                uint16_t offset; //in pages (not bytes) of this page within the VmObject
                uint16_t wireCount;
                void* vmo;
            } vm;

            struct
            {
                uint16_t validCount;
            } mmu;
        };
    };

    using PageList = sl::FwdList<PageInfo, &PageInfo::mmList>;

    struct MemoryDomain
    {
        Paddr physOffset;
        PageInfo* infoDb;
        uintptr_t pmaBase;

        MmuSpace* kernelSpace;
        Paddr zeroPage;
        Core::Waitable onHighMemoryPressure;

        struct
        {
            sl::RunLevelLock<RunLevel::Dpc> lock;
            size_t pageCount;
            PageList free {}; //unused pages that contain non-zero/unknown data
            PageList zeroed {}; //unused pages that are zeroed and ready for immediate use
        } freeLists;
        
        struct
        {
            sl::RunLevelLock<RunLevel::Dpc> lock;
            PageList active {}; //mapped and directly accessible by client programs
            PageList dirty {}; //unmapped, contain data that is newer than data in backing store.
            PageList standby {}; //unmapped, data is identical to backing store but still present in memory.
        } liveLists;
    };
}
