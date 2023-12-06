#include <memory/VmObject.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Memory
{
    Mdl::~Mdl()
    {
        vmm->ReleaseMdl(base.raw);
    }

    VmObject::VmObject(VMM* vmm, size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits)
    {
        if (vmm == nullptr)
            vmm = &VMM::Kernel();
        this->vmm = vmm;

        base = nullptr;
        size = 0;

        auto maybeBase = vmm->Alloc(length, initArg, flags, limits);
        VALIDATE(maybeBase.HasValue(),, "VMO creation failed");
        base = *maybeBase;
        size = length;
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
            vmm->Free(base.raw);
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

    VmObject VmObject::Subdivide(size_t length, bool fromStart)
    {
        if (length > size)
            return {};

        auto maybeSplit = vmm->Split(base.raw, fromStart ? length : size - length);
        if (!maybeSplit.HasValue())
            return {};

        VmObject other {};
        other.vmm = vmm;
        if (fromStart)
        {
            other.base = base;
            other.size = *maybeSplit;
            base.raw += other.size;
            size -= other.size;
        }
        else
        {
            other.base = base.raw + *maybeSplit;
            other.size = *maybeSplit;
            size -= other.size;
        }

        if (size == 0)
            base = nullptr;

        return other;
    }
}
