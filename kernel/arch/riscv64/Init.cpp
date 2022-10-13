#include <arch/Platform.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <boot/LimineBootstrap.h>
#include <debug/Log.h>
#include <memory/Vmm.h>

namespace Npk
{
    void InitCore(size_t id)
    {
        VMM::Kernel().MakeActive();
        
        BlockSumac();

        //load stvec
        //create core local block
        //calibrate timer for bsp

        Log("Core %lu finished core init.", LogLevel::Info, id);
    }

    void ApEntry(limine_smp_info* info)
    {
        // InitCore(info->hart_id);
        ExitApInit();
    }
}

extern "C"
{
    struct EntryData
    {
        uintptr_t hartId;
        uintptr_t dtb;
        uintptr_t physBase;
        uintptr_t virtBase;
    };

    void KernelEntry(EntryData* data)
    {
        using namespace Npk;
        
        WriteCsr("sscratch", 0);
        Boot::PerformLimineBootstrap(data->physBase, data->virtBase, data->hartId, data->dtb);
        data = nullptr;
        (void)data;

        InitEarlyPlatform();
        InitMemory();
        InitPlatform();

        Halt();
    }
}
