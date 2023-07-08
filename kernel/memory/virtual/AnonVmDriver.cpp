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
        features.demandPage = enableFeatures & AnonFeature::Demand;
        features.zeroPage = enableFeatures & AnonFeature::ZeroPage;

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

        Log("VmDriver init: anon, demandPageIn=%s, zeroPage=%s", LogLevel::Info, 
            features.demandPage ? "yes" : "no", features.zeroPage ? "yes" : "no");
    }

    EventResult AnonVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        //check that the type of access is legal according to the VMM.
        //Note that the VMM may not have passed the full permissions on to the HAT, this is so
        //we can have the HAT trigger page faults on certain actions. This lets us interact with a
        //program as it uses it's virtual memory (demand paging, swapping).
        if (flags.Has(VmFaultFlag::Write) && !context.range.flags.Has(VmFlag::Write))
            return EventResult::Kill;
        if (flags.Has(VmFaultFlag::Execute) && !context.range.flags.Has(VmFlag::Execute))
            return EventResult::Kill;
        if (flags.Has(VmFaultFlag::User) && !context.range.flags.Has(VmFlag::User))
            return EventResult::Kill;

        //if we've reached this point, the fault wasn't caused by a permissions violation,
        //so we map some usable memory here and return to the program.
        const size_t hatMode = context.range.token;
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

        return EventResult::Continue;
    }

    AttachResult AnonVmDriver::Attach(VmDriverContext& context, uintptr_t attachArg)
    {
        const bool demandAllowed = !CoresInEarlyInit() && CoreLocalAvailable() && features.demandPage;
        const bool doDemand = demandAllowed && !(attachArg & AnonFeature::Demand);
        const bool doZeroPage = demandAllowed && features.zeroPage && !(attachArg & AnonFeature::ZeroPage);

        //determine what HAT mode we'll use for translation. TODO: naturally use larger modes
        const size_t hatMode = 0;
        const size_t hatGranuleSize = GetHatLimits().modes[hatMode].granularity;
        const AttachResult result 
        { 
            .success = true, 
            .token = hatMode, 
            .baseOffset = 0,
            .deadLength = sl::AlignUp(context.range.length, hatGranuleSize)
        };

        if (doDemand && !doZeroPage)
            return result; //traditional demand paging, fault on first read/write access.

        VmFlags flags = context.range.flags;
        if (doZeroPage)
            flags.Clear(VmFlag::Write); //tell MMU to fault on next write to this page

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += hatGranuleSize)
        {
            const uintptr_t phys = doZeroPage ? zeroPage : PMM::Global().Alloc();
            Map(context.map, context.range.base + i, phys, 0, ConvertFlags(flags), false);
        };

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
