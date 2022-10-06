#include <memory/virtual/KernelVmDriver.h>
#include <arch/Paging.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>

namespace Npk::Memory::Virtual
{
    void KernelVmDriver::Init()
    {
        //use the biggest pages we can for the hhdm, with a limit of 1GiB pages.
        //any bigger and we might clobber them. TODO: revist this in the future.
        const PageSizes hhdmSize = MaxSupportedPagingSize() > PageSizes::_1G ? PageSizes::_1G: MaxSupportedPagingSize();
        const uintptr_t hhdmPageSize = GetPageSize(hhdmSize);

        for (uintptr_t i = 0; i < hhdmLength; i += hhdmPageSize)
            MapMemory(kernelMasterTables, hhdmBase + i, i, PageFlags::Write, hhdmSize, false);
        
        //map the kernel binary
        auto MapSection = [&](uintptr_t addr, size_t length, PageFlags flags)
        {
            const auto* resp = Boot::kernelAddrRequest.response;
            length = sl::AlignUp(addr + length, PageSize) - addr;
            addr = sl::AlignDown(addr, PageSize);
            
            for (uintptr_t i = addr; i < addr + length; i += PageSize)
                MapMemory(kernelMasterTables, i, i - resp->virtual_base + resp->physical_base, flags, PageSizes::_4K, false);
        };

        //map the program segments from the kernel binary with appropriate permissions.
        MapSection((uintptr_t)KERNEL_TEXT_BEGIN, (size_t)KERNEL_TEXT_SIZE, PageFlags::Execute);
        MapSection((uintptr_t)KERNEL_RODATA_BEGIN, (size_t)KERNEL_RODATA_SIZE, PageFlags::None);
        MapSection((uintptr_t)KERNEL_DATA_BEGIN, (size_t)KERNEL_DATA_SIZE, PageFlags::Write);

        //update kernel tables generation
        __atomic_add_fetch(&kernelTablesGen, 1, __ATOMIC_RELEASE);
    }

    VmDriverType KernelVmDriver::Type()
    { return VmDriverType::Kernel; }

    EventResult KernelVmDriver::HandleEvent(VmDriverContext& context, EventType type, uintptr_t eventArg)
    {
        ASSERT_UNREACHABLE();
    }
    
    sl::Opt<size_t> KernelVmDriver::AttachRange(VmDriverContext& context, uintptr_t attachArg)
    {
        sl::ScopedLock ptLock(context.lock);
        
        //attachArg is the physical address to map.
        for (size_t i = 0; i < context.length; i += PageSize)
            MapMemory(context.ptRoot, context.vaddr + i, attachArg + i, PageFlags::Write, PageSizes::_4K, false);
        
        if (context.ptRoot == kernelMasterTables)
            __atomic_add_fetch(&kernelTablesGen, 1, __ATOMIC_RELEASE);

        return 0;
    }
    
    bool KernelVmDriver::DetachRange(VmDriverContext& context)
    {
        sl::ScopedLock ptLock(context.lock);

        for (uintptr_t base = context.vaddr; base < context.vaddr + context.length;)
        {
            uintptr_t ignored;
            PageSizes ignored2;
            UnmapMemory(context.ptRoot, base, ignored, ignored2, true);
        }

        return true;
    }
}
