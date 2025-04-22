#pragma once

#include <KernelTypes.hpp>

namespace Npk
{
    void InitBspLapic(InitState& state);
    void SignalEoi();
}
