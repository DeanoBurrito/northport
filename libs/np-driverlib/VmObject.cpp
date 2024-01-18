#include <VmObject.h>
#include <Log.h>
#include <interfaces/driver/Memory.h>

namespace dl
{
    //checks to ensure our c++ enum flags match the C API ones.
    static_assert(VmFlags(VmFlag::Write).Raw() == VmWrite);
    static_assert(VmFlags(VmFlag::Execute).Raw() == VmExecute);
    static_assert(VmFlags(VmFlag::User).Raw() == VmUser);
    static_assert(VmFlags(VmFlag::Guarded).Raw() == VmGuarded);
    static_assert(VmFlags(VmFlag::Anon).Raw() == VmAnon);
    static_assert(VmFlags(VmFlag::Mmio).Raw() == VmMmio);
    static_assert(VmFlags(VmFlag::File).Raw() == VmFile);

    static_assert(VmFlagTypeMask == (size_t)NPK_VM_FLAG_TYPE_MASK);

    VmObject::VmObject(size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits)
    {
        const npk_vm_flags apiFlags = static_cast<npk_vm_flags>(flags.Raw());
        npk_vm_limits apiLimits = {};
        npk_vm_limits* apiLimitsPtr = nullptr;

        if (limits.lowerBound != 0 || limits.upperBound != -1ul || limits.alignment != 1)
        {
            apiLimits = { limits.lowerBound, limits.upperBound, limits.alignment};
            apiLimitsPtr = &apiLimits;
        }

        base = nullptr;
        base = npk_vm_alloc(length, reinterpret_cast<void*>(initArg), apiFlags, apiLimitsPtr);
        VALIDATE_(base.ptr != nullptr, );
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
    }

    VmObject& VmObject::operator=(VmObject&& from)
    {
        sl::Swap(this->base, from.base);
        sl::Swap(this->size, from.size);

        return *this;
    }

    void VmObject::Release()
    {
        if (base.ptr == nullptr)
            return;
        
        VALIDATE_(npk_vm_free(base.ptr), );
        base.ptr = nullptr;
        size = 0;
    }

    VmFlags VmObject::Flags(sl::Opt<VmFlags> flags)
    {
        if (flags.HasValue())
        {
            auto apiFlags = static_cast<npk_vm_flags>(flags->Raw());
            VALIDATE_(npk_vm_set_flags(base.ptr, apiFlags), {});
        }

        npk_vm_flags outFlags {};
        VALIDATE_(npk_vm_get_flags(base.ptr, &outFlags), {});
        return VmFlags(static_cast<size_t>(outFlags));
    }

    VmObject VmObject::Subdivide(size_t length, bool fromStart)
    {
        if (length >= size)
            return {};

        uintptr_t offset = fromStart ? length : size - length;
        VALIDATE_(npk_vm_split(base.ptr, &offset), {});

        VmObject other {};
        if (fromStart)
        {
            other.base = base;
            other.size = offset;
            base.raw += other.size;
            size -= other.size;
        }
        else
        {
            other.base = base.raw + offset;
            other.size = offset;
            size -= other.size;
        }

        if (size == 0)
            base = nullptr;

        return other;
    }
}
