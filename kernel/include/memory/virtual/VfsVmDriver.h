#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    enum class VfsFeature : uintptr_t
    {
        Demand = 1 << 0,
    };

    class VfsVmDriver : public VmDriver
    {
    private:
        struct 
        {
            bool demandPage;
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

