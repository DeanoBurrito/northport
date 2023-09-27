#include <memory/VmObject.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Memory
{
    VmObject::VmObject(size_t length, uintptr_t arg, VmFlags flags)
    {
        vmm = &VMM::Kernel(); //TODO: overridable
        auto maybeBase = vmm->Alloc(length, arg, flags);
        if (maybeBase)
        {
            base = *maybeBase;
            size = length;
        }
        else
            Log("VMO creation failed, caller: 0x%lx", LogLevel::Error, (uintptr_t)__builtin_return_address(0));
    }

    VmObject::~VmObject()
    {
        Release();
    }

    VmObject::VmObject(VmObject&& from)
    {
        sl::Swap(this->base, from.base);
        sl::Swap(this->size, from.size);
        sl::Swap(this->vmm, from.vmm);
    }

    VmObject& VmObject::operator=(VmObject&& from)
    {
        sl::Swap(this->base, from.base);
        sl::Swap(this->size, from.size);
        sl::Swap(this->vmm, from.vmm);
        
        return *this;
    }

    void VmObject::Release()
    {
        if (base.ptr != nullptr)
            VMM::Kernel().Free(base.raw);
        base.ptr = nullptr;
        size = 0;
    }

    VmFlags VmObject::Flags(sl::Opt<VmFlags> flags)
    {
        if (flags.HasValue())
        {
            if (!vmm->SetFlags(base.raw, *flags))
                Log("Failed to update VMO flags: base=0x%lx, size=0x%lx, flags=0x%lx",
                    LogLevel::Error, base.raw, size, flags->Raw());
        }

        const auto maybeFlags = vmm->GetFlags(base.raw, size);
        if (!maybeFlags.HasValue())
        {
            Log("Failed to get VMO flags: base=0x%lx, size=0x%lx", LogLevel::Error,
                base.raw, size);
            return {};
        }
        return *maybeFlags;
    }
}
