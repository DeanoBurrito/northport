#include <memory/virtual/KernelVmDriver.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>

namespace Npk::Memory::Virtual
{
    void KernelVmDriver::Init()
    {
        PageSizes hhdmSize = MaxSupportedPagingSize();
        while (GetPageSize(hhdmSize) > hhdmLength)
            hhdmSize = (PageSizes)((size_t)hhdmSize - 1);
        
        //map the hhdm.
        for (uintptr_t i = 0; i < hhdmLength; i += GetPageSize(hhdmSize))
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

    EventResult KernelVmDriver::HandleEvent(VmDriverContext& context, EventType type, uintptr_t addr, uintptr_t eventArg)
    {
        (void)context; (void)type; (void)addr; (void)eventArg;
        ASSERT_UNREACHABLE();
    }
    
    sl::Opt<size_t> KernelVmDriver::AttachRange(VmDriverContext& context, uintptr_t attachArg)
    {
        InterruptGuard guard;
        sl::ScopedLock ptLock(context.lock);
        
        //attachArg is the physical address to map.
        for (size_t i = 0; i < context.range.length; i += PageSize)
            MapMemory(context.ptRoot, context.range.base + i, attachArg + i, PageFlags::Write, PageSizes::_4K, false);
        
        if (context.ptRoot == kernelMasterTables)
            __atomic_add_fetch(&kernelTablesGen, 1, __ATOMIC_RELEASE);

        return 0;
    }
    
    bool KernelVmDriver::DetachRange(VmDriverContext& context)
    {
        InterruptGuard guard;
        sl::ScopedLock ptLock(context.lock);

        for (uintptr_t base = context.range.base; base < context.range.base + context.range.length;)
        {
            uintptr_t ignored;
            PageSizes size;
            if (UnmapMemory(context.ptRoot, base, ignored, size, true))
                base += GetPageSize(size);
            else
                base += PageSize;

        }
        
        if (context.ptRoot == kernelMasterTables)
            __atomic_add_fetch(&kernelTablesGen, 1, __ATOMIC_RELEASE);

        return true;
    }
}
