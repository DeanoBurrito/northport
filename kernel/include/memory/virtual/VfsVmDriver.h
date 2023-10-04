#pragma once

#include <filesystem/Vfs.h>
#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    struct VfsVmLink
    {
        VfsVmLink* next;

        size_t fileOffset;
        sl::Handle<Filesystem::Node> vfsNode;
        bool readonly;
    };

    enum class VfsFeature : uintptr_t
    {
        FaultHandler = 1 << 0,
    };

    class VfsVmDriver : public VmDriver
    {
    private:
        struct 
        {
            bool faultHandler;
        } features;

    public:
        void Init(uintptr_t enableFeatures) override;

        EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) override;
        bool ModifyRange(VmDriverContext& context, ModifyRangeArgs args) override;
        SplitResult Split(VmDriverContext& context, size_t offset) override;
        QueryResult Query(size_t length, VmFlags flags, uintptr_t attachArg) override;
        AttachResult Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg) override;
        bool Detach(VmDriverContext& context) override;
    };
}

