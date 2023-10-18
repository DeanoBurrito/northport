#include <memory/virtual/AnonVmDriver.h>
#include <memory/Pmm.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <Maths.h>
#include <Memory.h>

namespace Npk::Memory::Virtual
{
    constexpr size_t FaultMaxMapAhead = 8;
    
    void AnonVmDriver::Init(uintptr_t enableFeatures)
    {
        //extract enabled features
        features.faultHandler = enableFeatures & (uintptr_t)AnonFeature::FaultHandler;
        features.zeroPage = enableFeatures & (uintptr_t)AnonFeature::ZeroPage;

        if (features.zeroPage)
        {
            //initialize zero page:
            //This is a primitive version of copy-on-write, until we have anon-objects.
            //When memory is requested its immediately backed, but with a readonly
            //page of all zeroes. Upon writing to this page the page is remapped to
            //a freshly allocated one. This can help reduce kernel entries for page
            //faults on reads to anon memory, and this only occurs the first time a write
            //happens.
            //If demand paging is enabled and zero page is not, then the driver will leave
            //ranges completely unmapped until accessed, before allocating usable memory,
            //as you'd expect from traditional demand paging.
            zeroPage = PMM::Global().Alloc();
            sl::memset((void*)AddHhdm(zeroPage), 0, PageSize);
        }

        Log("VmDriver init: anon, faultHandler=%s, zeroPage=%s", LogLevel::Info, 
            features.faultHandler ? "yes" : "no", features.zeroPage ? "yes" : "no");
    }

    EventResult AnonVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        //if we've reached this point, the fault wasn't caused by a permissions violation,
        //so we map some usable memory here and return to the program.
        (void)flags;
        const size_t hatMode = reinterpret_cast<size_t>(context.range.token);
        const size_t granuleSize = GetHatLimits().modes[hatMode].granularity;
        const size_t mapLength = sl::Min(FaultMaxMapAhead * granuleSize, context.range.Top() - where);
        const size_t mapCount = sl::AlignUp(mapLength, granuleSize) / granuleSize;
        where = sl::AlignDown(where, granuleSize);

        const auto convFlags = ConvertFlags(context.range.flags);
        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < mapCount; i++)
        {
            //NOTE: right now the only reason we'd be getting a fault is due to demand
            //paging/zero paging, so if this logic seems to make a lot of assumptions - thats why.
            const uintptr_t paddr = PMM::Global().Alloc();
            ASSERT(granuleSize == PageSize, "TODO: allocate an appropriate amount of physical memory for the granule size");
            if (features.zeroPage)
                SyncMap(context.map, where + i * granuleSize, paddr, convFlags, true);
            else
                ASSERT_(Map(context.map, where + i * granuleSize, paddr, hatMode, convFlags, false));
        }

        context.stats.anonResidentSize += mapCount * granuleSize;
        return { .goodFault = true };
    }

    bool AnonVmDriver::ModifyRange(VmDriverContext& context, ModifyRangeArgs args)
    { 
        ASSERT_(args.trimStart == 0 && args.trimEnd == 0); //TODO: not yet implemented

        if (args.setFlags.Has(VmFlag::Guarded) || args.clearFlags.Has(VmFlag::Guarded))
            return false;

        const size_t hatMode = reinterpret_cast<size_t>(context.range.token);
        const size_t granuleSize = GetHatLimits().modes[hatMode].granularity;
        const bool doFlush = args.clearFlags.Any() || HatLimits().flushOnPermsUpgrade;

        HatFlags flags = ConvertFlags(context.range.flags);
        flags &= ~ConvertFlags(args.clearFlags);
        flags |= ConvertFlags(args.setFlags);

        sl::ScopedLock lock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
            SyncMap(context.map, context.range.base + i, {}, flags, doFlush);

        return true;
    }

    SplitResult AnonVmDriver::Split(VmDriverContext& context, size_t offset)
    {
        const size_t hatMode = reinterpret_cast<size_t>(context.range.token);
        const size_t granuleSize = GetHatLimits().modes[hatMode].granularity;

        offset = sl::AlignUp(offset, granuleSize);
        if (offset > context.range.length)
            return { .success = false };

        const SplitResult result
        {
            .offset = offset,
            .tokenLow = context.range.token,
            .tokenHigh = context.range.token,
            .success = true,
        };

        return result;
    }

    QueryResult AnonVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        (void)attachArg;
        QueryResult result;
        result.success = true;
        
        const HatLimits limits = GetHatLimits();
        result.hatMode = 0;
        result.alignment = limits.modes[result.hatMode].granularity;
        result.length = sl::AlignUp(length, result.alignment);

        if (flags.Has(VmFlag::Guarded))
            result.length += 2 * result.alignment;

        return result;
    }

    AttachResult AnonVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        //determine what features have access to: this depends on a few environmental factors
        //as well as the features enabled when this driver was started, and optionally some may
        //be disabled just for this VM range.
        //Environmental factors include not using demand paging while other cores are not set up
        //for it (since buffers like the heap can be shared between cores).
        const bool demandAllowed = !CoresInEarlyInit() && CoreLocalAvailable() 
            && features.faultHandler;
        const bool doDemand = demandAllowed && !(attachArg & (uintptr_t)AnonFeature::FaultHandler);
        const bool doZeroPage = demandAllowed && features.zeroPage 
            && !(attachArg & (uintptr_t)AnonFeature::ZeroPage);

        const AttachResult result
        {
            .token = reinterpret_cast<void*>(query.hatMode),
            .offset = 0,
            .success = true,
        };
        context.stats.anonWorkingSize += context.range.length - result.offset;

        if (doDemand && !doZeroPage)
            return result;

        VmFlags flags = context.range.flags;
        if (doZeroPage)
            flags.Clear(VmFlag::Write); //fault on next write to this page

        const HatFlags hatFlags = ConvertFlags(flags);
        const size_t granuleSize = GetHatLimits().modes[query.hatMode].granularity;

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            const uintptr_t phys = doZeroPage ? zeroPage : PMM::Global().Alloc();
            Map(context.map, context.range.base + i, phys, 0, hatFlags, false);
        }

        context.stats.anonResidentSize += context.range.length;
        return result;
    }

    bool AnonVmDriver::Detach(VmDriverContext& context)
    {
        context.stats.anonWorkingSize -= context.range.length - context.range.offset;
        const HatLimits& hatLimits = GetHatLimits();

        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < context.range.length;)
        {
            uintptr_t phys;
            size_t mode;
            if (Unmap(context.map, context.range.base, phys, mode, true))
            {
                const size_t length = hatLimits.modes[mode].granularity;
                if (phys != zeroPage)
                    PMM::Global().Free(phys, length / PageSize);
                i += length;
                context.stats.anonResidentSize -= length;
            }
            else
                i += hatLimits.modes[0].granularity; //nothing was mapped, try the next area.
        }
        
        return true;
    }
}
