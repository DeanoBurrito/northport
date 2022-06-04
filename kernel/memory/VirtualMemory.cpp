#include <memory/VirtualMemory.h>
#include <scheduling/Thread.h>
#include <Format.h>
#include <String.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Memory
{
    /*
        The current implementation of our VMM makes a few assumptions:
        - VM ranges cannot overlap.
        - Ranges are stored in order of their base address.
        - It requires the HHDM for certain functions.
    */

    VMRange VirtualMemoryManager::InsertRange(VMRange range, bool backImmediately)
    {
        auto appendAfter = ranges.Begin();
        while (appendAfter != ranges.End() && appendAfter->base < range.base)
            ++appendAfter;

        if (appendAfter == ranges.End())
            ranges.Append(range);
        else
            ranges.InsertAfter(appendAfter, range);

        if (!sl::EnumHasFlag(range.flags, MemoryMapFlags::ForceUnmapped) && backImmediately)
            pageTables.MapRange(range.base, range.length / PAGE_FRAME_SIZE, range.flags);
        
        return range;
    }

    VMRange VirtualMemoryManager::FindRange(size_t length, NativeUInt lowerBound, NativeUInt upperBound)
    {
        NativeUInt base = 0; //TODO: fix me, this should be using a while. It dosnt handle empty ranges, or when all ranges are below the target
        
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            base = it->base + it->length;
            if (base < lowerBound)
                continue;
            if (base + length >= upperBound)
                return {}; //no valid space before we hit the upper bound
            
            auto next = it;
            ++next;

            if (next->base - it->base >= length)
                return { base, length };
        }

        return {};
    }

    VirtualMemoryManager* VirtualMemoryManager::Current()
    {
        if (CPU::ReadMsr(MSR_GS_BASE) == 0 || GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].ptr == nullptr)
            return nullptr;
        else
            return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Scheduling::Thread>()->Parent()->VMM();
    }

    void VirtualMemoryManager::Init()
    {
        sl::SpinlockRelease(&rangesLock);
        pageTables.InitClone();
    }

    void VirtualMemoryManager::Deinit()
    {
        ranges.Clear();
        pageTables.Teardown();
    }
    
    void VirtualMemoryManager::MakeActive()
    {
        pageTables.MakeActive();
    }

    bool VirtualMemoryManager::IsActive() const
    {
        return pageTables.IsActive();
    }

    bool VirtualMemoryManager::AddRange(VMRange range, bool backNow)
    {
        if (!backNow)
        {
            //TODO: implement demand-paging
            Log("Demand paging not implemented, VM ranges must be alloc-now.", LogSeverity::Error); 
            return false;
        }

        //align the base down, and the length up to the nearest pages
        if (range.base % PAGE_FRAME_SIZE != 0)
            range.base = (range.base / PAGE_FRAME_SIZE) * PAGE_FRAME_SIZE;
        if (range.length % PAGE_FRAME_SIZE != 0)
            range.length = (range.length / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;

        sl::ScopedSpinlock scopeLock(&rangesLock);
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (it->base >= range.base + range.length)
                continue;
            if (it->base + it->length <= range.base)
                continue;

            Logf("Cannot add VM range (base=0x%lx, len=0x%lx), collisions occured with base=0x%lx, len=0x%lx", LogSeverity::Error, range.base, range.length, it->base, it->length);
            return false;
        }

        //everything is okay, lets add the range and back it with some entries in the page tables.
        InsertRange(range, true);
        return true;
    }

    bool VirtualMemoryManager::RemoveRange(VMRange range)
    {
        sl::ScopedSpinlock scopeLock(&rangesLock);
        auto foundRange = ranges.End();
        
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (it->base != range.base || it->length != range.length)
                continue;
        }

        if (foundRange == ranges.End())
            return false;

        if (!sl::EnumHasFlag(foundRange->flags, MemoryMapFlags::ForceUnmapped)
            && !sl::EnumHasFlag(foundRange->flags, MemoryMapFlags::ForeignMemory))
            pageTables.UnmapRange(foundRange->base, foundRange->length / PAGE_FRAME_SIZE);
        ranges.Remove(foundRange);
        return true;
    }

    void VirtualMemoryManager::ModifyRange(VMRange range, MemoryMapFlags flags)
    {
        Log("Not implemented: ModifyRange", LogSeverity::Fatal);
    }

    void VirtualMemoryManager::ModifyRange(VMRange range, int adjustLength, bool fromEnd)
    {
        Log("Not implemented: ModifyRange", LogSeverity::Fatal);
    }

    VMRange VirtualMemoryManager::AllocRange(size_t length, bool backNow, MemoryMapFlags flags, NativeUInt lowerBound, NativeUInt upperBound)
    {
        if (length % PAGE_FRAME_SIZE != 0)
            length = (length / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;
        
        sl::ScopedSpinlock scopeLock(&rangesLock);
        VMRange range = FindRange(length, lowerBound, upperBound);
        if (range.base == 0)
            return {};
        
        range.flags = flags;
        return InsertRange(range, backNow);
    }

    VMRange VirtualMemoryManager::AllocMmioRange(sl::NativePtr physBase, size_t length, MemoryMapFlags flags)
    {
        if (length % PAGE_FRAME_SIZE != 0)
            length = (length / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;
        
        sl::ScopedSpinlock scopeLock(&rangesLock);
        VMRange range = FindRange(length, 0, (uint64_t)-1);
        if (range.base == 0)
            return {};
        
        range.flags = flags;
        InsertRange(range, false); //dont allocate physical memory for this
        pageTables.MapRange(range.base, physBase, length / PAGE_FRAME_SIZE, flags);
        
        return range;
    }

    VMRange VirtualMemoryManager::AddSharedRange(VirtualMemoryManager& foreignVmm, VMRange foreignRange)
    {
        sl::ScopedSpinlock scopeLock(&rangesLock);
        VMRange range = FindRange(foreignRange.length, 0, (NativeUInt)-1);
        range.flags = foreignRange.flags | MemoryMapFlags::ForeignMemory;
        InsertRange(range, false);
        
        for (size_t offset = 0; offset < range.length; offset += PAGE_FRAME_SIZE)
        {
            auto maybeForeignPhys = foreignVmm.pageTables.GetPhysicalAddress(foreignRange.base + offset);
            if (!maybeForeignPhys)
            {
                Log("Could not mapped shared physical VM range, missing physical page in remote vmm.", LogSeverity::Error);
                return {};
            }

            pageTables.MapMemory(range.base + offset, maybeForeignPhys->raw + offset, foreignRange.flags | MemoryMapFlags::ForeignMemory);
        }

        return range;
    }

    sl::Opt<sl::NativePtr> VirtualMemoryManager::GetPhysAddr(sl::NativePtr ptr)
    {
        if (ptr.ptr == nullptr)
            return {};

        return pageTables.GetPhysicalAddress(ptr);
    }

    size_t VirtualMemoryManager::CopyInto(sl::BufferView sourceBuffer, sl::NativePtr destBase)
    {
        if (!RangeExists({destBase.raw, sourceBuffer.length}))
            return 0;
        
        size_t lengthCopied = 0;
        while (lengthCopied < sourceBuffer.length)
        {
            auto maybePhys = pageTables.GetPhysicalAddress(destBase.raw + lengthCopied);
            if (!maybePhys)
                return lengthCopied;
            
            if (lengthCopied == 0)
            {
                //first copy may not be page-aligned at the destination
                size_t lengthToCopy = PAGE_FRAME_SIZE;
                if (destBase.raw % PAGE_FRAME_SIZE != 0)
                    lengthToCopy -= destBase.raw % PAGE_FRAME_SIZE;
                
                sl::memcopy(sourceBuffer.base.As<void>(), EnsureHigherHalfAddr(maybePhys->ptr), lengthToCopy);
                lengthCopied += lengthToCopy;
            }
            else if (lengthCopied + PAGE_FRAME_SIZE > sourceBuffer.length)
            {
                const size_t lengthToCopy = sourceBuffer.length - lengthCopied;
                sl::memcopy(sourceBuffer.base.As<void>(lengthCopied), EnsureHigherHalfAddr(maybePhys->ptr), lengthToCopy);
                lengthCopied += lengthToCopy;
                return lengthCopied;
            }
            else
            {
                sl::memcopy(sourceBuffer.base.As<void>(lengthCopied), EnsureHigherHalfAddr(maybePhys->ptr), PAGE_FRAME_SIZE);
                lengthCopied += PAGE_FRAME_SIZE;
            }
        }

        return sourceBuffer.length;
    }

    size_t VirtualMemoryManager::CopyFrom(sl::BufferView sourceBuffer, sl::NativePtr destBase)
    {
        Log("CopyFrom() not implemented. TODO:", LogSeverity::Fatal);
    }

    bool VirtualMemoryManager::RangeExists(VMRange range)
    { 
        return RangeExists(range, MemoryMapFlags::None);
    }

    bool VirtualMemoryManager::RangeExists(VMRange range, MemoryMapFlags minFlags)
    { 
        constexpr size_t flagsMask = 0b001111; //only check for flags we care about
        const size_t maskedFlags = (size_t)minFlags & flagsMask;
        
        sl::ScopedSpinlock scopeLock(&rangesLock);
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (range.base >= it->base && range.base + range.length <= it->base + it->length)
            {
                if (minFlags == MemoryMapFlags::None)
                    return true;
                
                return (maskedFlags & (size_t)it->flags) == maskedFlags;
            }
        }

        return false;
    }

    void VirtualMemoryManager::PrintRanges(sl::NativePtr highlightRangeOf)
    {
        for (auto range = ranges.Begin(); range != ranges.End(); ++range)
        {
            const sl::String logStr = sl::FormatToString("VM Range: start=0x%lx, end=0x%lx, length=0x%lx, flags=0x%lx", 0, 
                range->base, range->base + range->length, range->length, (uint64_t)range->flags);
            
            if (highlightRangeOf.ptr != nullptr && 
                highlightRangeOf.raw >= range->base && highlightRangeOf.raw < range->base + range->length)
                Log(logStr.C_Str(), LogSeverity::Debug);
            else
                Log(logStr.C_Str(), LogSeverity::Info);
        }
    }
}
