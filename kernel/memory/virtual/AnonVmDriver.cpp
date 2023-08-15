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
        //check that the type of access is legal according to the VMM.
        //Note that the VMM may not have passed the full permissions on to the HAT, this is so
        //we can have the HAT trigger page faults on certain actions. This lets us interact with a
        //program as it uses it's virtual memory (demand paging, swapping).
        if (flags.Has(VmFaultFlag::Write) && !context.range.flags.Has(VmFlag::Write))
            return { .goodFault = false };
        if (flags.Has(VmFaultFlag::Execute) && !context.range.flags.Has(VmFlag::Execute))
            return { .goodFault = false };
        if (flags.Has(VmFaultFlag::User) && !context.range.flags.Has(VmFlag::User))
            return { .goodFault = false };

        //if we've reached this point, the fault wasn't caused by a permissions violation,
        //so we map some usable memory here and return to the program.
        const size_t hatMode = reinterpret_cast<size_t>(context.range.token);
        const size_t granuleSize = GetHatLimits().modes[hatMode].granularity;
        const size_t mapLength = sl::Min(FaultMaxMapAhead, context.range.Top() - where);
        const size_t mapCount = sl::AlignUp(mapLength, granuleSize) / granuleSize;
        where = sl::AlignDown(where, granuleSize);

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < mapCount; i++)
        {
            Map(context.map, where + (i * granuleSize), PMM::Global().Alloc(),
                hatMode, ConvertFlags(context.range.flags), true);
        }

        return { .goodFault = true };
    }

    bool AnonVmDriver::ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags)
    { 
        ASSERT_UNREACHABLE();
    }

    QueryResult AnonVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
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

        return result;
    }

    bool AnonVmDriver::Detach(VmDriverContext& context)
    {
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
            }
            else
                i += hatLimits.modes[0].granularity; //nothing was mapped, try the next area.
        }
        
        return true;
    }
}
