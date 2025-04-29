#include <hardware/Arch.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <hardware/x86_64/Mmu.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <KernelApi.hpp>
#include <Memory.h>
#include <NanoPrintf.h>

namespace Npk
{
    struct TrapFrame {};

    extern "C" void InterruptDispatch(TrapFrame* frame)
    {
        (void)frame;
        NPK_UNREACHABLE();
    }
}

namespace Npk
{
    SL_TAGGED(cpubase, CoreLocalHeader localHeader);

    void SetMyLocals(uintptr_t where, CpuId softwareId)
    {
        auto tls = reinterpret_cast<CoreLocalHeader*>(where);
        tls->swId = softwareId;
        tls->selfAddr = where;
        tls->currThread = nullptr;

        WriteMsr(Msr::GsBase, where);
        Log("Cpu %zu locals at %p", LogLevel::Info, softwareId, tls);
    }

    bool CheckForDebugcon();
    bool CheckForCom1();

    void ArchInitEarly()
    {
        if (!CheckForDebugcon())
            CheckForCom1();
    }

    void ArchInitDomain0(InitState& state)
    { (void)state; } //no-op

    void ArchInitFull(uintptr_t& virtBase)
    {
        InitBspLapic(virtBase);
    }

    KernelMap ArchSetKernelMap(sl::Opt<KernelMap> next)
    {
        const KernelMap prev = READ_CR(3);

        const Paddr future = next.HasValue() ? *next : kernelMap;
        WRITE_CR(3, future);

        return prev;
    }
}
