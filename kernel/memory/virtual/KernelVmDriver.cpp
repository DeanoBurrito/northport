#include <memory/virtual/KernelVmDriver.h>
#include <debug/Log.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>

namespace Npk::Memory::Virtual
{
    void KernelVmDriver::Init()
    {
        //map the kernel binary itself into the kernel map
        auto MapSection = [&](uintptr_t addr, size_t length, HatFlags flags)
        {
            const auto* resp = Boot::kernelAddrRequest.response;
            length = sl::AlignUp(addr + length, PageSize) - addr;
            addr = sl::AlignDown(addr, PageSize);
            
            for (uintptr_t i = addr; i < addr + length; i += PageSize)
                Map(KernelMap(), i, i - resp->virtual_base + resp->physical_base, 0, flags, false);
        };

        //mapping the HHDM belongs here as well, but it's performed as part of the HAT init,
        //since we can optimize those mappings differently per-architecture. See an implemenation Hat.cpp
        //for details.

        //map the program segments from the kernel binary with appropriate permissions.
        MapSection((uintptr_t)KERNEL_TEXT_BEGIN, (size_t)KERNEL_TEXT_SIZE, HatFlags::Execute | HatFlags::Global);
        MapSection((uintptr_t)KERNEL_RODATA_BEGIN, (size_t)KERNEL_RODATA_SIZE, HatFlags::None | HatFlags::Global);
        MapSection((uintptr_t)KERNEL_DATA_BEGIN, (size_t)KERNEL_DATA_SIZE, HatFlags::Write | HatFlags::Global);
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
        sl::InterruptGuard guard; //TODO: is this needed?
        sl::ScopedLock ptLock(context.lock);
        
        HatFlags flags = HatFlags::Global;
        if (VmFlags::Write == (context.range.flags & VmFlags::Write))
            flags |= HatFlags::Write;
        
        for (size_t i = 0; i < context.range.length; i += PageSize)
            Map(context.map, context.range.base + i, attachArg + i, 0, flags, false);

        return 0;
    }
    
    bool KernelVmDriver::DetachRange(VmDriverContext& context)
    {
        const HatLimits& hatLimits = GetHatLimits();

        sl::InterruptGuard guard;
        sl::ScopedLock ptLock(context.lock);

        for (uintptr_t base = context.range.base; base < context.range.base + context.range.length;)
        {
            uintptr_t ignored;
            size_t mode;
            if (Unmap(context.map, base, ignored, mode, true))
                base += hatLimits.modes[mode].granularity;
            else
                base += hatLimits.modes[0].granularity;
        }
        
        return true;
    }
}
