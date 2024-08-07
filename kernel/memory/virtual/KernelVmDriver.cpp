#include <memory/virtual/KernelVmDriver.h>
#include <debug/Log.h>
#include <boot/LinkerSyms.h>
#include <interfaces/loader/Generic.h>
#include <Maths.h>

namespace Npk::Memory::Virtual
{
    VmRange kernelRanges[3];

    sl::Span<VmRange> GetKernelRanges()
    { return kernelRanges; }

    void KernelVmDriver::Init(uintptr_t enableFeatures)
    {
        (void)enableFeatures;
        const uintptr_t physBase = GetKernelPhysAddr();
        ASSERT_(physBase != 0);
        const uintptr_t virtBase = (uintptr_t)KERNEL_BLOB_BEGIN;

        //map the kernel binary itself into the kernel map
        auto MapSection = [&](VmRange& range, uintptr_t addr, size_t length, VmFlags vmFlags, HatFlags flags)
        {
            length = sl::AlignUp(addr + length, PageSize);
            addr = sl::AlignDown(addr, PageSize);
            length -= addr;

            range.base = addr;
            range.length = length;
            range.flags = vmFlags | VmFlag::Mmio;
            range.mdlCount = 1;
            
            for (uintptr_t i = addr; i < addr + length; i += PageSize)
                HatDoMap(KernelMap(), i, i - virtBase + physBase, 0, flags, false);
        };

        //mapping the HHDM belongs here as well, but it's performed as part of the HAT init,
        //since we can optimize those mappings differently per-architecture. See an implemenation Hat.cpp
        //for details.

        //map the program segments from the kernel binary with appropriate permissions.
        MapSection(kernelRanges[0], (uintptr_t)KERNEL_TEXT_BEGIN, (size_t)KERNEL_TEXT_SIZE, VmFlag::Execute, HatFlags::Execute | HatFlags::Global);
        MapSection(kernelRanges[1], (uintptr_t)KERNEL_RODATA_BEGIN, (size_t)KERNEL_RODATA_SIZE, {}, HatFlags::Global);
        MapSection(kernelRanges[2], (uintptr_t)KERNEL_DATA_BEGIN, (size_t)KERNEL_DATA_SIZE, VmFlag::Write, HatFlags::Write | HatFlags::Global);

        Log("VmDriver init: kernel", LogLevel::Info);
    }

    EventResult KernelVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        ASSERT_UNREACHABLE();
        (void)context; (void)where; (void)flags;
    }

    bool KernelVmDriver::ModifyRange(VmDriverContext& context, ModifyRangeArgs args)
    {
        ASSERT_UNREACHABLE();
        (void)context; (void)args;
    }

    SplitResult KernelVmDriver::Split(VmDriverContext& context, size_t offset)
    {
        const uintptr_t alignment = HatGetLimits().modes[0].granularity;
        offset = sl::AlignUp(offset, alignment);

        if (offset > context.range.length)
            return { .success = false };

        SplitResult result
        {
            .offset = offset,
            .tokenLow = nullptr,
            .tokenHigh = nullptr,
            .success = true,
        };

        return result;
    }

    QueryResult KernelVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        (void)attachArg;

        QueryResult result;
        result.success = true;
        result.hatMode = 0;
        result.alignment = HatGetLimits().modes[result.hatMode].granularity;
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
            HatDoMap(context.map, context.range.base + i, attachArg + i, query.hatMode, flags, false);

        context.stats.mmioWorkingSize += context.range.length;
        return result;
    }
    
    bool KernelVmDriver::Detach(VmDriverContext& context)
    {
        const HatLimits& hatLimits = HatGetLimits();
        sl::ScopedLock ptLock(context.lock);

        for (uintptr_t base = context.range.base; base < context.range.base + context.range.length;)
        {
            uintptr_t ignored;
            size_t mode;
            if (HatDoUnmap(context.map, base, ignored, mode, true))
            {
                base += hatLimits.modes[mode].granularity;
                context.stats.mmioWorkingSize -= hatLimits.modes[mode].granularity;
            }
            else
                base += hatLimits.modes[0].granularity;
        }
        
        return true;
    }
}
