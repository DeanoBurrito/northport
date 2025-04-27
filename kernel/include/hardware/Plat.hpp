#pragma once

namespace Npk
{
    struct InitState;

    void PlatInitEarly();
    void PlatInitDomain0(InitState& state);
    void PlatInitFull(uintptr_t& virtBase);
}
