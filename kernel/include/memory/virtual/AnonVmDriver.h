#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    enum AnonFeature : uintptr_t
    {
        Demand = 1 << 0,
        ZeroPage = 1 << 1,
    };

    class AnonVmDriver : public VmDriver
    {
    private:
        uintptr_t zeroPage;

        struct
        {
            bool demandPage;
            bool zeroPage;
        } features;

    public:
        void Init(uintptr_t enableFeatures) override;

        EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) override;
        AttachResult Attach(VmDriverContext& context, uintptr_t attachArg) override;
        bool Detach(VmDriverContext& context) override;
    };
}
