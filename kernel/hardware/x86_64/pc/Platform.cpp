#include <hardware/Plat.hpp>

#include <KernelApi.hpp>

namespace Npk
{
    void PlatInitEarly()
    {} //no-op

    void PlatInitDomain0(InitState& state)
    { (void)state; } //no-op

    void PlatInitFull(uintptr_t& virtBase)
    { (void)virtBase; } //no-op
}
