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
        sl::Swap(this->size, this->size);
    }

    VmObject& VmObject::operator=(VmObject&& from)
    {
        sl::Swap(this->base, from.base);
        sl::Swap(this->size, from.size);
        
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
            if (!VMM::Kernel().SetFlags(base.raw, size, *flags))
                Log("Failed to set VMO flags: base=0x%lx, len=0x%lx, flags=0x%lx", LogLevel::Error,
                    base.raw, size, flags->Raw());
        }
        return *VMM::Kernel().GetFlags(base.raw, size);
    }
}
