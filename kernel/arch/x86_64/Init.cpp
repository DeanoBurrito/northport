#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/x86_64/Timers.h>
#include <arch/Platform.h>
#include <arch/Cpu.h>
#include <boot/LimineTags.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>

namespace Npk
{
    void InitCore(size_t id)
    {
        BlockSumac();
        FlushGdt();
        LoadIdt();

        uint64_t cr0 = ReadCr0();
        cr0 |= 1 << 16;
        WriteCr0(cr0); //set write-protect bit

        uint64_t cr4 = ReadCr4();
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21; //SMA enable: prevent accessing user pages as the supervisor while AC is clear
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20; //Prevent executing from user pages while in ring 0.
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11; //prevents system store instructions in user mode (sidt/sgdt)
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7; //global pages: pages that are global.
        WriteCr4(cr4);

        if (CpuHasFeature(CpuFeature::NoExecute))
            WriteMsr(MsrEfer, ReadMsr(MsrEfer) | (1 << 11));

        //my stack overfloweth (alt title: maximum maintainability).
        Log("Core %lu enabled features: write-protect%s%s%s%s%s.", LogLevel::Info, 
            id,
            cr4 & (1 << 21) ? ", smap" : "",
            cr4 & (1 << 20) ? ", smep" : "",
            cr4 & (1 << 11) ? ", umip" : "",
            cr4 & (1 << 7) ? ", global-pages" : "",
            CpuHasFeature(CpuFeature::NoExecute) ? ", nx" : "");

        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = id;
        clb->selfAddr = (uintptr_t)clb;
        clb->interruptControl = (uintptr_t)new LocalApic();
        WriteMsr(MsrGsBase, (uint64_t)clb);

        LocalApic::Local().Init();
        Log("Core %lu finished core init.", LogLevel::Info, id);
    }
}

extern "C"
{
    void KernelEntry()
    {
        using namespace Npk;

        InitEarlyPlatform();
        InitMemory();
        InitPlatform();

        IoApic::InitAll();
        PopulateIdt();
        InitTimers();

        InitCore(0);

        Log("Kernel done for now.", LogLevel::Info);
        Halt();
    }
}
