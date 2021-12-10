#include <Log.h>
#include <Cpu.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
#include <memory/KernelHeap.h>
#include <acpi/AcpiTables.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <boot/Stivale2.h>

namespace Kernel
{
    stivale2_struct* stivale2Struct;
    stivale2_tag* FindStivaleTagInternal(uint64_t id)
    {
        stivale2_tag* tag = sl::NativePtr(stivale2Struct->tags).As<stivale2_tag>();

        while (tag != nullptr)
        {
            if (tag->identifier == id)
                return tag;
            
            tag = sl::NativePtr(tag->next).As<stivale2_tag>();
        }

        return nullptr;
    }

    template<typename TagType>
    TagType FindStivaleTag(uint64_t id)
    {
        return reinterpret_cast<TagType>(FindStivaleTagInternal(id));
    }
    
    void InitMemory()
    {
        using namespace Memory;

        Log("Initializing memory ...", LogSeverity::Info);
        
        stivale2_struct_tag_memmap* mmap = FindStivaleTag<stivale2_struct_tag_memmap*>(STIVALE2_STRUCT_TAG_MEMMAP_ID);
        
        PMM::Global()->Init(mmap);
        PageTableManager::Setup();
        PageTableManager::Local()->Init(true); //TODO: move away from bootloader map, and use our own
        PageTableManager::Local()->MakeActive();
        PMM::Global()->InitLate();

        //assign heap to start immediately after last mapped kernel page
        stivale2_struct_tag_pmrs* pmrs = FindStivaleTag<stivale2_struct_tag_pmrs*>(STIVALE2_STRUCT_TAG_PMRS_ID);
        size_t tentativeHeapStart = 0;
        for (size_t i = 0; i < pmrs->entries; i++)
        {
            stivale2_pmr* pmr = &pmrs->pmrs[i];
            const size_t pmrTop = pmr->base + pmr->length;

            if (pmrTop > tentativeHeapStart)
                tentativeHeapStart = pmrTop;
        }

        KernelHeap::Global()->Init(tentativeHeapStart);
        Log("Memory init complete.", LogSeverity::Info);
    }

    void InitPlatform()
    {
        Log("Initializing platform ...", LogSeverity::Info);

        SetupGDT();
        FlushGDT();
        Log("GDT successfully installed.", LogSeverity::Verbose);

        SetupIDT();
        LoadIDT();
        Log("IDT successfully installed.", LogSeverity::Verbose);

        stivale2_struct_tag_rsdp* stivaleRsdpTag = FindStivaleTag<stivale2_struct_tag_rsdp*>(STIVALE2_STRUCT_TAG_RSDP_ID);
        ACPI::AcpiTables::Global()->Init(stivaleRsdpTag->rsdp);
        
        Log("Platform init complete.", LogSeverity::Info);
    }
}

extern "C"
{
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;
        stivale2Struct = stivaleStruct;

        CPU::ClearInterruptsFlag();
        CPU::DoCpuId();

        LoggingInitEarly();
#ifdef NORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT
        EnableLogDestinaton(LogDestination::DebugCon);
#endif
#ifdef NORTHPORT_ENABLE_FRAMEBUFFER_LOG_AT_BOOT 
        EnableLogDestinaton(LogDestination::FramebufferOverwrite);
#endif

        Log("", LogSeverity::EnumCount); //log empty line so the output of debugcon/serial is starting in a known place.
        Log("Northport kernel succesfully started.", LogSeverity::Info);

        InitMemory();
        LoggingInitFull();
        InitPlatform();

        Log("Kernel init done.", LogSeverity::Info);
        
        for (;;)
            asm("hlt");
    }
}
