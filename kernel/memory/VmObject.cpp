#include <memory/VmObject.h>
#include <memory/Vmm.h>
#include <debug/Log.h>

namespace Npk::Memory
{
    VmObject::VmObject(size_t length, uintptr_t arg, VmFlags flags)
    {
        auto maybeBase = VMM::Kernel().Alloc(length, arg, flags);
        if (maybeBase)
        {
            base = maybeBase->base;
            size = maybeBase->length;
        }
        else
            Log("VMO creation failed, caller: 0x%lx", LogLevel::Error, (uintptr_t)__builtin_return_address(1));
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
}
