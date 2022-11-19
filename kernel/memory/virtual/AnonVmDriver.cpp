#include <memory/virtual/AnonVmDriver.h>
#include <memory/Pmm.h>
#include <arch/Platform.h>
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

        //check it's not a permission violation
        if ((flags & VmFaultFlags::Write) == VmFaultFlags::Write && (context.range.flags & VmFlags::Write) == VmFlags::None)
            return EventResult::Kill; //tried to write to non-writable VmRegion.
        if ((flags & VmFaultFlags::Execute) == VmFaultFlags::Execute && (context.range.flags & VmFlags::Execute) == VmFlags::None)
            return EventResult::Kill; //tried to execute from an invalid range
        if ((flags & VmFaultFlags::User) == VmFaultFlags::User && (context.range.flags & VmFlags::User) == VmFlags::None)
            return EventResult::Kill; //user tried to access supervisor range

        //everything checks out, map it and back it.
        const size_t loadCount = sl::Min(FaultMaxLoadAhead, (context.range.base + context.range.length - pageAddr) / PageSize);
        InterruptGuard guard;
        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < loadCount; i++)
            MapMemory(context.ptRoot, pageAddr + i * PageSize, PMM::Global().Alloc(), ConvertFlags(context.range.flags), PageSizes::_4K, false);
        
        if (context.ptRoot == kernelMasterTables)
            __atomic_add_fetch(&kernelTablesGen, 1, __ATOMIC_RELEASE);
        
        return EventResult::Continue;
    }

    sl::Opt<size_t> AnonVmDriver::AttachRange(VmDriverContext& context, uintptr_t attachArg)
    {
        if (attachArg == 0)
            return 0;
        
        InterruptGuard guad;
        sl::ScopedLock scopeLock(context.lock);

        for (size_t i = 0; i < context.range.length / PageSize; i++)
            MapMemory(context.ptRoot, context.range.base + i * PageSize, PMM::Global().Alloc(), ConvertFlags(context.range.flags), PageSizes::_4K, false);
        return 0;
    }

    bool AnonVmDriver::DetachRange(VmDriverContext& context)
    {
        Log("Anon VM driver is freeing memory? TODO:", LogLevel::Debug);
        return true;
    }
}
