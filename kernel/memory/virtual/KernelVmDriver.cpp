#include <memory/virtual/KernelVmDriver.h>
#include <debug/Log.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>

namespace Npk::Memory::Virtual
{
    void KernelVmDriver::Init(uintptr_t enableFeatures)
    {
        (void)enableFeatures;

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

        Log("VmDriver init: kernel", LogLevel::Info);
    }

    EventResult KernelVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE();
        (void)context; (void)where; (void)flags;
    }
    
    AttachResult KernelVmDriver::Attach(VmDriverContext& context, uintptr_t attachArg)
    {
        HatFlags flags = HatFlags::Global;
        if (context.range.flags.Has(VmFlag::Write))
            flags |= HatFlags::Write;

        const size_t hatMode = 0;
        const size_t hatGranuleSize = GetHatLimits().modes[hatMode].granularity;

        AttachResult result
        {
            .success = true,
            .token = 0,
            .baseOffset = attachArg % hatGranuleSize, //handle non-page-aligned mmio
            .deadLength = sl::AlignUp(result.baseOffset + context.range.length, hatGranuleSize)
        };

        attachArg = sl::AlignDown(attachArg, hatGranuleSize);
        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < result.deadLength; i += hatGranuleSize)
            Map(context.map, context.range.base + i, attachArg + i, hatMode, flags, false);

        return result;
    }
    
    bool KernelVmDriver::Detach(VmDriverContext& context)
    {
        const HatLimits& hatLimits = GetHatLimits();

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
