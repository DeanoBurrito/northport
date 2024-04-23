#include <interfaces/driver/Memory.h>
#include <interfaces/Helpers.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>

extern "C"
{
    using namespace Npk;

    DRIVER_API_FUNC
    void* npk_heap_alloc(size_t count)
    {
        return Memory::Heap::Global().Alloc(count);
    }

    DRIVER_API_FUNC
    void npk_heap_free(void* ptr, size_t count)
    {
        Memory::Heap::Global().Free(ptr, count);
    }

    DRIVER_API_FUNC
    size_t npk_pm_alloc_size()
    {
        return PageSize;
    }

    DRIVER_API_FUNC
    uintptr_t npk_hhdm_base()
    {
        return hhdmBase;
    }

    DRIVER_API_FUNC
    size_t npk_hhdm_limit()
    {
        return hhdmLength;
    }

    DRIVER_API_FUNC
    uintptr_t npk_pm_alloc(OPTIONAL npk_pm_limits* limits)
    {
        ASSERT_(limits == nullptr);
        
        return PMM::Global().Alloc();
    }

    DRIVER_API_FUNC
    uintptr_t npk_pm_alloc_many(size_t count, OPTIONAL npk_pm_limits* limits)
    {
        ASSERT_(limits == nullptr); //TODO: support me!

        return PMM::Global().Alloc(count);
    }

    DRIVER_API_FUNC
    bool npk_pm_free(uintptr_t paddr)
    {
        PMM::Global().Free(paddr);
        return true;
    }

    DRIVER_API_FUNC
    bool npk_pm_free_many(uintptr_t paddr, size_t count)
    {
        PMM::Global().Free(paddr, count);
        return true;
    }

    DRIVER_API_FUNC
    void* npk_vm_alloc(size_t length, void* arg, npk_vm_flags flags, OPTIONAL npk_vm_limits* limits)
    {
        ASSERT_(limits == nullptr); //TODO: support

        VmFlags vmFlags(flags);
        auto maybeArg = VMM::Kernel().Alloc(length, reinterpret_cast<uintptr_t>(arg), vmFlags);
        return maybeArg.HasValue() ? reinterpret_cast<void*>(*maybeArg) : nullptr;
    }

    DRIVER_API_FUNC
    bool npk_vm_free(void* vm_ptr)
    {
        return VMM::Kernel().Free(reinterpret_cast<uintptr_t>(vm_ptr));
    }

    DRIVER_API_FUNC
    bool npk_vm_acquire_mdl(REQUIRED npk_mdl* mdl, void* vaddr, size_t length)
    {
        if (mdl == nullptr)
            return false;

        auto handle = VMM::Kernel().AcquireMdl(reinterpret_cast<uintptr_t>(vaddr), length);
        if (!handle.Valid())
            return false;

        handle->references++;
        //TODO: stash handle so it remains valid after this call
        mdl->addr_space = nullptr; //TODO: allow for mdls to access non-kernel VMMs
        mdl->virt_base = reinterpret_cast<uintptr_t>(vaddr);
        mdl->length = length;
        mdl->ptr_count = handle->ptrs.Size();

        //TODO: some way of marshalling the existing structs rather than creating a copy for driver use?
        mdl->ptrs = new npk_mdl_ptr[mdl->ptr_count];
        for (size_t i = 0; i < mdl->ptr_count; i++)
        {
            mdl->ptrs[i].length = handle->ptrs[i].length;
            mdl->ptrs[i].phys_base = handle->ptrs[i].physAddr;
        }

        return true;
    }

    DRIVER_API_FUNC
    void npk_vm_release_mdl(void* vaddr)
    {
        VMM::Kernel().ReleaseMdl(reinterpret_cast<uintptr_t>(vaddr));
    }

    DRIVER_API_FUNC
    bool npk_vm_get_flags(void* vm_ptr, REQUIRED npk_vm_flags* flags)
    {
        if (flags == nullptr)
            return false;

        auto maybeFlags = VMM::Kernel().GetFlags(reinterpret_cast<uintptr_t>(vm_ptr));
        if (!maybeFlags.HasValue())
            return false;

        *flags = static_cast<npk_vm_flags>(maybeFlags->Raw());
        return true;
    }

    DRIVER_API_FUNC
    bool npk_vm_set_flags(void* vm_ptr, npk_vm_flags flags)
    {
        ASSERT_UNREACHABLE();
    }

    DRIVER_API_FUNC
    bool npk_vm_split(void* vm_ptr, REQUIRED uintptr_t* offset)
    {
        ASSERT_UNREACHABLE();
    }
}
