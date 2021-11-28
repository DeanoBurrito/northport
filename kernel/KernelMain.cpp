#include <Log.h>
#include <Cpu.h>
#include <Maths.h>
#include <memory/PhysicalMemory.h>
#include <memory/Paging.h>
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
        
        stivale2_struct_tag_memmap* mmap = (stivale2_struct_tag_memmap*)FindStivaleTag(stivaleStruct, STIVALE2_STRUCT_TAG_MEMMAP_ID);
        PMM::Global()->Init(mmap);

        PageTableManager::Setup();
        PageTableManager::Local()->Init(true);

        //TODO: move away from bootloader map, and use our own

        PageTableManager::Local()->MakeActive();
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
        
        for (;;)
            asm("hlt");
    }
}
