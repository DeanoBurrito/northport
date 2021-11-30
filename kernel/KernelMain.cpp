#include <Log.h>
#include <Cpu.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <arch/x86_64/Gdt.h>
#include <boot/Stivale2.h>

namespace Kernel
{
    stivale2_tag* FindStivaleTag(stivale2_struct* stivaleStruct, uint64_t id)
    {
        stivale2_tag* tag = sl::NativePtr(stivaleStruct->tags).As<stivale2_tag>();

        while (tag != nullptr)
        {
            if (tag->identifier == id)
                return tag;
            
            tag = sl::NativePtr(tag->next).As<stivale2_tag>();
        }

        return nullptr;
    }
    
    void InitMemory(stivale2_struct* stivaleStruct)
    {
        using namespace Memory;

        Log("Initializing memory ...", LogSeverity::Info);
        
        stivale2_struct_tag_memmap* mmap = (stivale2_struct_tag_memmap*)FindStivaleTag(stivaleStruct, STIVALE2_STRUCT_TAG_MEMMAP_ID);
        PMM::Global()->Init(mmap);

        PageTableManager::Setup();
        PageTableManager::Local()->Init(true);

        //TODO: move away from bootloader map, and use our own

        PageTableManager::Local()->MakeActive();

        //assign heap to start immediately after last mapped kernel page
        stivale2_struct_tag_pmrs* pmrs = (stivale2_struct_tag_pmrs*)FindStivaleTag(stivaleStruct, STIVALE2_STRUCT_TAG_PMRS_ID);
        size_t tentativeHeapStart = 0;
        for (size_t i = 0; i < pmrs->entries; i++)
        {
            stivale2_pmr* pmr = &pmrs->pmrs[i];
            const size_t pmrTop = pmr->base + pmr->length;

            if (pmrTop > tentativeHeapStart)
                tentativeHeapStart = pmrTop;
        }

        KernelHeap::Global()->Init(tentativeHeapStart);
    }

    void InitPlatform()
    {
        Log("Initializing platform ...", LogSeverity::Info);

        SetupGDT();
        FlushGDT();
        Log("GDT successfully installed.", LogSeverity::Verbose);
    }
}

extern "C"
{
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;

        CPU::ClearInterruptsFlag();
        CPU::DoCpuId();

        LoggingInitEarly();
        EnableLogDestinaton(LogDestination::DebugCon);
        Log("", LogSeverity::EnumCount); //log empty line so the output of debugcon/serial is starting in a known place.
        Log("Northport kernel succesfully started.", LogSeverity::Info);

        InitMemory(stivaleStruct);
        InitPlatform();

        Log("Kernel init done.", LogSeverity::Info);
        
        for (;;)
            asm("hlt");
    }
}
