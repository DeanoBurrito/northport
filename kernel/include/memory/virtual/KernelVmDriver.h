#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    class KernelVmDriver : public VmDriver
    {
    public:
        void Init() override;
        VmDriverType Type() override;

        EventResult HandleEvent(VmDriverContext& context, EventType type, uintptr_t eventArg) override;
        sl::Opt<size_t> AttachRange(VmDriverContext& context, uintptr_t attachArg) override;
        bool DetachRange(VmDriverContext& context) override;
    };
}
