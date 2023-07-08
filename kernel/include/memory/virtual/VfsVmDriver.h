#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    class VfsVmDriver : public VmDriver
    {
    public:
        void Init(uintptr_t enableFeatures) override;

        EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) override;
        AttachResult Attach(VmDriverContext& context, uintptr_t attachArg) override;
        bool Detach(VmDriverContext& context) override;
    };
}

