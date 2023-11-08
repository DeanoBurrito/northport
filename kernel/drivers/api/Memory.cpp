#include <drivers/api/Memory.h>
#include <memory/Pmm.h>
#include <memory/Vmm.h>
#include <memory/Heap.h>

extern "C"
{
    using namespace Npk;

    [[gnu::used]]
    void* npk_heap_alloc(size_t count)
    {
        return Memory::Heap::Global().Alloc(count);
    }

    [[gnu::used]]
    void npk_heap_free(void* ptr, size_t count)
    {
        Memory::Heap::Global().Free(ptr, count);
    }

    [[gnu::used]]
    uintptr_t npk_hhdm_base()
    {
        return hhdmBase;
    }

    [[gnu::used]]
    uintptr_t npk_pm_alloc(OPTIONAL npk_pm_limits* limits)
    {
        ASSERT_(limits == nullptr);
        
        return PMM::Global().Alloc();
    }

    [[gnu::used]]
    uintptr_t npk_pm_alloc_many(size_t count, OPTIONAL npk_pm_limits* limits)
    {
        ASSERT_(limits == nullptr); //TODO: support me!

        return PMM::Global().Alloc(count);
    }

    [[gnu::used]]
    bool npk_pm_free(uintptr_t paddr)
    {
        PMM::Global().Free(paddr);
        return true;
    }

    [[gnu::used]]
    bool npk_pm_free_many(uintptr_t paddr, size_t count)
    {
        PMM::Global().Free(paddr, count);
        return true;
    }

    [[gnu::used]]
    void* npk_vm_alloc(size_t length, void* arg, npk_vm_flags flags, OPTIONAL npk_vm_limits* limits)
    {
        ASSERT_(limits == nullptr); //TODO: support

        VmFlags vmFlags(flags);
        auto maybeArg = VMM::Kernel().Alloc(length, reinterpret_cast<uintptr_t>(arg), vmFlags);
        return maybeArg.HasValue() ? reinterpret_cast<void*>(*maybeArg) : nullptr;
    }

    [[gnu::used]]
    bool npk_vm_free(void* vm_ptr)
    {
        return VMM::Kernel().Free(reinterpret_cast<uintptr_t>(vm_ptr));
    }

    [[gnu::used]]
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

    [[gnu::used]]
    bool npk_vm_set_flags(void* vm_ptr, npk_vm_flags flags)
    {
        ASSERT_UNREACHABLE();
    }

    [[gnu::used]]
    bool npk_vm_split(void* vm_ptr, REQUIRED uintptr_t* offset)
    {
        ASSERT_UNREACHABLE();
    }
}
