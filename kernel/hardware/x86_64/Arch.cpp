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

    bool debugconDoColour;

    static void DebugconPutc(int c, void* ignored)
    {
        (void)ignored;
        Out8(Port::Debugcon, static_cast<uint8_t>(c));
    }

    static void DebugconWrite(LogSinkMessage msg)
    {
        constexpr const char FormatStr[] = "%.0s[%7s]%.0s ";
        constexpr const char ColourFormatStr[] = "%s[%7s]%s ";
        constexpr const char ResetColourStr[] = "\e[39m";

        const auto levelStr = LogLevelStr(msg.level);
        const char* format = debugconDoColour ? ColourFormatStr : FormatStr;
        const char* colourStr = [](LogLevel level) -> const char*
        {
            switch (level)
            {
            case LogLevel::Error:   return "\e[31m";
            case LogLevel::Warning: return "\e[33m";
            case LogLevel::Info:    return "\e[97m";
            case LogLevel::Verbose: return "\e[90m";
            case LogLevel::Trace:   return "\e[37m";
            case LogLevel::Debug:   return "\e[34m";
            }
        }(msg.level);

        npf_pprintf(DebugconPutc, nullptr, format, colourStr, levelStr.Begin(), ResetColourStr);

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
