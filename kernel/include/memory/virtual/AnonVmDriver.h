#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    enum class AnonFeature : uintptr_t
    {
        FaultHandler = 1 << 0,
        ZeroPage = 1 << 1,
    };

    class AnonVmDriver : public VmDriver
    {
    private:
        uintptr_t zeroPage;

        struct
        {
            bool faultHandler;
            bool zeroPage;
        } features;

    public:
        void Init(uintptr_t enableFeatures) override;

        EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) override;
        bool ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags) override;
        QueryResult Query(size_t length, VmFlags flags, uintptr_t attachArg) override;
        AttachResult Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg) override;
        bool Detach(VmDriverContext& context) override;
    };
}
