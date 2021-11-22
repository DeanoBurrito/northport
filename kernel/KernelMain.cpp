#include <Log.h>
#include <memory/PhysicalMemory.h>
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
        
        stivale2_tag* mmapTag = FindStivaleTag(stivaleStruct, STIVALE2_STRUCT_TAG_MEMMAP_ID);
        PMM::Global()->Init(reinterpret_cast<stivale2_struct_tag_memmap*>(mmapTag));
    }
}

extern "C"
{
    void _KernelEntry(stivale2_struct* stivaleStruct)
    {
        using namespace Kernel;

        LoggingInitEarly();
        EnableLogDestinaton(LogDestination::DebugCon);
        Log("Northport kernel succesfully started.", LogSeverity::Info);

        InitMemory(stivaleStruct);
        
        for (;;)
            asm("hlt");
    }
}
