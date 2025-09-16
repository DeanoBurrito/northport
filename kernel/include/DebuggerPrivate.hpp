#pragma once

#include <Debugger.hpp>
#include <Flags.hpp>
#include <Atomic.hpp>
#include <Locks.hpp>

namespace Npk::Private
{
    extern bool debuggerInitialized;
    extern sl::Flags<DebugEventType> enabledEvents;

    extern sl::SpinLock debugTransportsLock;
    extern DebugTransportList debugTransports;

    extern DebugProtocol* debugProtocol;
    extern DebugProtocol gdbProtocol;
}
