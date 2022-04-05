#include <memory/VirtualMemory.h>
#include <memory/PhysicalMemory.h>
#include <scheduling/Thread.h>
#include <Log.h>
#include <Maths.h>

namespace Kernel::Memory
{
    VirtualMemoryManager* VirtualMemoryManager::Current()
    {
        if (CPU::ReadMsr(MSR_GS_BASE) == 0 || GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].ptr == nullptr)
            return nullptr;
        else
            return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Scheduling::Thread>()->Parent()->VMM();
    }

    sl::Vector<VMRange> VirtualMemoryManager::InsertRange(NativeUInt base, size_t length, MemoryMapFlags flags)
    { 
        sl::Vector<VMRange> damageRanges;
        auto insertAfter = ranges.Begin();
        
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            //check if we've found a better insert point
            if (it->base < base && it->base > insertAfter->base)
                insertAfter = it;
            
            if (it->base > base + length) //range is above affected area
                continue;
            if (it->base + it->length < base) //range is below affected area
                continue;

            //we're got an intersection, calculate the overlap and store it
            const NativeUInt intersectBase = sl::max<NativeUInt>(it->base, base);
            const NativeUInt intersectTop = sl::min<NativeUInt>(it->base + it->length, base + length);

            if (intersectTop - intersectBase > 0)
                damageRanges.PushBack(VMRange(it->flags, intersectBase, intersectTop - intersectBase));
        }

        //finally, insert our new range into the list
        if (insertAfter == ranges.End())
            ranges.Append(VMRange(flags, base, length));
        else
            ranges.InsertAfter(insertAfter, VMRange(flags, base, length));

        return damageRanges;
    }

    sl::Vector<VMRange> VirtualMemoryManager::DestroyRange(NativeUInt base, size_t length)
    { 
        sl::Vector<VMRange> damageRanges;
        
        for (auto it = ranges.Begin(); it != ranges.End(); ++it)
        {
            if (it->base > base + length) //range is above affected area
                continue;
            if (it->base + it->length < base) //range is below affected area
                continue;

            //we're got an intersection, calculate the overlap and store it
            const NativeUInt intersectBase = sl::max<NativeUInt>(it->base, base);
            const NativeUInt intersectTop = sl::min<NativeUInt>(it->base + it->length, base + length);

            damageRanges.PushBack(VMRange(it->flags, intersectBase, intersectTop - intersectBase));
        }

        //TODO: actually remove the range

        return damageRanges;
    }

    void VirtualMemoryManager::Init()
    {
        pageTables.InitClone();
    }

    void VirtualMemoryManager::Deinit()
    {
        pageTables.Teardown();
    }

    PageTableManager& VirtualMemoryManager::PageTables()
    { return pageTables; }

    void VirtualMemoryManager::AddRange(NativeUInt base, size_t length, MemoryMapFlags flags)
    {
        if (length % PAGE_FRAME_SIZE != 0)
            length = (length / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;
        if (base % PAGE_FRAME_SIZE != 0)
            base = (base / PAGE_FRAME_SIZE) * PAGE_FRAME_SIZE;
        
        sl::Vector<VMRange> damagedRanges = InsertRange(base, length, flags);
        const size_t bitmapLenBytes = (length / PAGE_FRAME_SIZE + 1) / 8 + 1;
        uint8_t appliedBitmap[bitmapLenBytes];
        sl::memset(appliedBitmap, 0, bitmapLenBytes);

        //now adjust any memory mappings accordingly
        for (auto it = damagedRanges.Begin(); it != damagedRanges.End(); ++it)
        {
            MemoryMapFlags mergedFlags = it->flags | flags;

            //mark the areas we'll take care off in the bitmap, and apply flags
            for (size_t i = 0; i < it->length / PAGE_FRAME_SIZE + 1; i++)
            {
                if (it->base + i * PAGE_FRAME_SIZE < base)
                    continue; //we're too low
                if (it->base + it->length > base + length)
                    break; //we're too high
                
                //set the bit as processed, and then apply the flags only if they're different.
                sl::BitmapSet(appliedBitmap, (it->base - base) / PAGE_FRAME_SIZE + i);
                if (mergedFlags != it->flags)
                    pageTables.ModifyPageFlags(it->base + i * PAGE_FRAME_SIZE, mergedFlags, (size_t)-1);
            }
        }

        //check the bitmap for any clear bits, apply the flags to those pages
        for (NativeUInt i = base; i < base + length; i += PAGE_FRAME_SIZE)
        {
            const size_t index = (i - base) / PAGE_FRAME_SIZE;
            
            if (sl::BitmapGet(appliedBitmap, index))
                continue;

            //apply the flags directly, and since its not a part of any other range we dont need to modify existing data
            pageTables.MapMemory(i, flags);
        }
    }

    bool VirtualMemoryManager::RemoveRange(NativeUInt base)
    { return false; }

    bool VirtualMemoryManager::RemoveRange(NativeUInt base, size_t length)
    { return false; }

    sl::NativePtr VirtualMemoryManager::AllocateRange(size_t length, MemoryMapFlags flags)
    {
        const size_t pagesNeeded = (length / PAGE_FRAME_SIZE + 1);
        sl::NativePtr physBase = Memory::PMM::Global()->AllocPages(pagesNeeded);

        return AllocateRange(physBase, length, flags);
    }

    sl::NativePtr VirtualMemoryManager::AllocateRange(sl::NativePtr physicalBase, size_t length, MemoryMapFlags flags)
    {
        if (length % PAGE_FRAME_SIZE != 0)
            length = (length / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;
        
        //TODO: this is pretty wasteful, it would be nice to implement a proper search.
        const size_t base = ranges.Last().base + ranges.Last().length;
        auto damageRanges = InsertRange(base, length, flags);
        if (!damageRanges.Empty())
        {
            Logf("Could not allocate range (base=0x%lx, length=0x%lx) as collisions occured.", LogSeverity::Error, base, length);
            return nullptr;
        }

        pageTables.MapRange(base, physicalBase, length / PAGE_FRAME_SIZE, flags);
        return base;
    }

    sl::NativePtr VirtualMemoryManager::AddSharedPhysicalRange(VirtualMemoryManager* foreignVmm, sl::NativePtr foreignAddr, size_t length, MemoryMapFlags localFlags)
    {
        //the other vmm is responsible for the physical memory, we're just creating a window to access it from here.
        localFlags = localFlags | MemoryMapFlags::NonOwning;

        //TODO: we use this in AllocateRange(), we should probably extract this into a separate function that just does that.
        const size_t base = ranges.Last().base + ranges.Last().length;

        size_t mappedLength = 0;
        while (mappedLength < length)
        {
            auto maybePhysAddr = foreignVmm->pageTables.GetPhysicalAddress(foreignAddr.raw + mappedLength);
            if (!maybePhysAddr)
            {
                Logf("Could not map physical range from foreign VMM, no physical memory @ 0x%lx", LogSeverity::Error, foreignAddr.raw);
                return nullptr; //TODO: add a range in the foreign vmm that encompasses this range, and then try again locally.
            }

            pageTables.MapMemory(base + mappedLength, maybePhysAddr->raw, localFlags);
            mappedLength += PAGE_FRAME_SIZE;
        }

        return base;
    }

    bool VirtualMemoryManager::RangeExists(NativeUInt base, size_t length)
    { return RangeExists(base, length, MemoryMapFlags::None); }

    bool VirtualMemoryManager::RangeExists(NativeUInt base, size_t length, MemoryMapFlags minFlags)
    {
        return true; //TODO: implement
    }

    void VirtualMemoryManager::PrintLog()
    {
        for (auto range = ranges.Begin(); range != ranges.End(); ++range)
        {
            Logf("VM range: start=0x%0lx end=0x%0lx length=0x%lx flags=0x%lx", LogSeverity::Info, range->base, range->base + range->length, range->length, (size_t)range->flags);
        }
    }
}
