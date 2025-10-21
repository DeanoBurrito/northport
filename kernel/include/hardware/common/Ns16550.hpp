#pragma once

#include <Types.hpp>
#include <Core.hpp>
#include <Debugger.hpp>

namespace Npk
{
    bool InitNs16550(uintptr_t& virtBase, Paddr regsBase, bool regsMmio);

    bool Ns16550Available();
    LogSink& Ns16550LogSink();
    DebugTransport& Ns16550DebugTransport();
}
