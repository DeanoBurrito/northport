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

    bool KernelVmDriver::ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags)
    {
        ASSERT_UNREACHABLE();
        (void)context; (void)flags;
    }

    QueryResult KernelVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        (void)attachArg;

        QueryResult result;
        result.success = true;
        result.hatMode = 0;
        result.alignment = GetHatLimits().modes[result.hatMode].granularity;
        result.length = sl::AlignUp(length, result.alignment);

        if (flags.Has(VmFlag::Guarded))
            result.length += result.alignment * 2;

        return result;
    }
    
    AttachResult KernelVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        HatFlags flags = HatFlags::Global;
        if (context.range.flags.Has(VmFlag::Write))
            flags |= HatFlags::Write;

        AttachResult result
        {
            .token = nullptr,
            .offset = attachArg % query.alignment,
            .success = true,
        };

        attachArg = sl::AlignDown(attachArg, query.alignment);
        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += query.alignment)
            Map(context.map, context.range.base + i, attachArg + i, query.hatMode, flags, false);

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
