#pragma once

#include <Debugger.hpp>
#include <Flags.hpp>
#include <Locks.hpp>

namespace Npk::Private
{
    extern CpuId debugCpusCount;
    extern bool debuggerInitialized;
    extern sl::Flags<DebugEventType> enabledEvents;

    extern sl::SpinLock debugTransportsLock;
    extern DebugTransportList debugTransports;

    extern DebugProtocol* debugProtocol;
    extern DebugProtocol gdbProtocol;

    void DebuggerPerCpuInit(void* arg);

    Breakpoint* AllocBreakpoint();
    void FreeBreakpoint(Breakpoint** bp);
    Breakpoint* GetBreakpointByAddr(uintptr_t addr);

    bool ArmBreakpoint(Breakpoint& bp);
    bool DisarmBreakpoint(Breakpoint& bp);
}
