#include <memory/virtual/AnonVmDriver.h>
#include <memory/Pmm.h>
#include <debug/Log.h>
#include <Maths.h>

namespace Npk::Memory::Virtual
{
    constexpr size_t FaultMaxLoadAhead = 8;
    
    void AnonVmDriver::Init()
    {}

    VmDriverType AnonVmDriver::Type()
    { return VmDriverType::Anon; }

    EventResult AnonVmDriver::HandleEvent(VmDriverContext& context, EventType type, uintptr_t addr, uintptr_t eventArg)
    {
        if (type != EventType::PageFault)
        {
            Log("Anon VM driver received unexpected event: %lu.", LogLevel::Error, (size_t)type);
            return EventResult::Kill;
        }

        const VmFaultFlags flags = (VmFaultFlags)eventArg;
        const size_t pageAddr = sl::AlignDown(addr, PageSize);

#define IS_BAD_ACCESS(type) \
        (flags & VmFaultFlags::type) == VmFaultFlags::type && (context.range.flags & VmFlags::type) == VmFlags::None
        
        //check for permission violations
        if (IS_BAD_ACCESS(Write))
            return EventResult::Kill; //tried to write to non-writable region.
        if (IS_BAD_ACCESS(Execute))
            return EventResult::Kill; //tried to exec from invalid region.
        if (IS_BAD_ACCESS(User))
            return EventResult::Kill; //user tried to access supervisor page.
#undef IS_BAD_ACCESS

        //everything checks out, map it and back it.
        const size_t loadCount = sl::Min(FaultMaxLoadAhead, ((context.range.base + context.range.length) - pageAddr) / PageSize);
        sl::InterruptGuard guard;
        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < loadCount; i++)
            Map(context.map, pageAddr + i * PageSize, PMM::Global().Alloc(), 0, ConvertFlags(context.range.flags), false);

        return EventResult::Continue;
    }

    sl::Opt<size_t> AnonVmDriver::AttachRange(VmDriverContext& context, uintptr_t attachArg)
    {
        attachArg = 1;
        //if core local block is not available, neither are interrupts (i.e. demand paging) so we back immediately anyway.
        if ((attachArg & 0b1) == 0 && CoreLocalAvailable())
            return attachArg;
        
        sl::InterruptGuard guard; //TODO: do we need this?
        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < context.range.length / PageSize; i++)
            Map(context.map, context.range.base + i * PageSize, PMM::Global().Alloc(), 0, ConvertFlags(context.range.flags), false);
        return attachArg;
    }

    bool AnonVmDriver::DetachRange(VmDriverContext& context)
    {
        const HatLimits& hatLimits = GetHatLimits();
        
        sl::InterruptGuard guard;
        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < context.range.length;)
        {
            uintptr_t phys;
            size_t mode;
            if (Unmap(context.map, context.range.base, phys, mode, true))
            {
                const size_t length = hatLimits.modes[mode].granularity;
                PMM::Global().Free(phys, length / PageSize);
                i += length;
            }
            else
                i += hatLimits.modes[0].granularity; //nothing was mapped, try the next area.
        }
        
        return true;
    }
}
