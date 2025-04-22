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
    { (void)frame; }
}

namespace Npk
{
    SL_TAGGED(cpubase, CoreLocalHeader localHeader);

    void SetMyLocals(uintptr_t where, CpuId softwareId)
    {
        const uintptr_t offset = where - (uintptr_t)&localHeader;
        WriteMsr(Msr::GsBase, offset);

        asm("mov %0, %%gs:0" :: "r"(softwareId));
        asm("mov %0, %%gs:8" :: "r"(where));
        Log("Cpu %zu locals at: abs=0x%tx, rel=0x%tx", LogLevel::Verbose, softwareId, where, offset);
    }
    static_assert(offsetof(CoreLocalHeader, swId) == 0);
    static_assert(offsetof(CoreLocalHeader, selfAddr) == 8);

    constexpr const char DebugconHeaderStr[] = "[%7s] ";
    constexpr const char DebugconHeaderColourStr[] = "%s[%7s]%s ";
    bool debugconDoColour;

    static void DebugconPutc(int c, void* ignored)
    {
        (void)ignored;
        Out8(Port::Debugcon, static_cast<uint8_t>(c));
    }

    static void DebugconWrite(LogSinkMessage msg)
    {
        const auto levelStr = LogLevelStr(msg.level);

        if (debugconDoColour)
            npf_pprintf(DebugconPutc, nullptr, DebugconHeaderColourStr, "", levelStr.Begin(), "");
        else
            npf_pprintf(DebugconPutc, nullptr, DebugconHeaderStr, levelStr.Begin());

        for (size_t i = 0; i < msg.text.Size(); i++)
            Out8(Port::Debugcon, msg.text[i]);

        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    static void DebugconReset()
    {
        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    LogSink debugconLogSink
    {
        .listHook = {},
        .Reset = DebugconReset,
        .Write = DebugconWrite
    };

    static void CheckForDebugcon()
    {
        //Afaik we cant check for debugcon directly, but we can check
        //for qemu (tcg/kvm) or bochs and assume it exists.
        //Far from perfect, but its helpful for now.

        debugconDoColour = ReadConfigUint("npk.x86.debugcon_do_colour", true);
        if (ReadConfigUint("npk.x86.debugcon_force_enable", false))
            return AddLogSink(debugconLogSink);

        if (!CpuHasFeature(CpuFeature::VGuest))
            return;

        CpuidLeaf leaf {};
        DoCpuid(0x4000'0000, 0, leaf);
        if (leaf.b != 0x4b4d564b || leaf.c != 0x564b4d56 || leaf.d != 0x4d)
            return;

        AddLogSink(debugconLogSink);
    }

    void ArchInitEarly()
    {
        CheckForDebugcon();
    }

    void ArchInit(InitState& state)
    { 
        InitBspLapic(state);
    }

    KernelMap ArchSetKernelMap(sl::Opt<KernelMap> next)
    {
        const KernelMap prev = READ_CR(3);

        if (next.HasValue())
            WRITE_CR(3, *next);

        return prev;
    }
}
