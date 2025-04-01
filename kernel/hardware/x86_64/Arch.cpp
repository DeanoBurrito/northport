#include <hardware/Arch.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PortIo.hpp>
#include <Kernel.hpp>
#include <Memory.h>

namespace Npk
{
    struct TrapFrame {};

    extern "C" void InterruptDispatch(TrapFrame* frame)
    {}
}

namespace Npk
{
    static void DebugconWrite(sl::StringSpan message, LogLevel level)
    {
        for (size_t i = 0; i < message.Size(); i++)
            Out8(Port::Debugcon, message[i]);

        Out8(Port::Debugcon, '\n');
        Out8(Port::Debugcon, '\r');
    }

    static void DebugconReset()
    {
        DebugconWrite("\n\r\n\r", LogLevel::Info);
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
}
