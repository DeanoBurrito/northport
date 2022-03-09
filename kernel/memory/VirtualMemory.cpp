#include <memory/VirtualMemory.h>
#include <scheduling/Thread.h>
#include <Maths.h>

namespace Kernel::Memory
{
    VirtualMemoryManager* VirtualMemoryManager::Current()
    {
        if (CPU::ReadMsr(MSR_GS_BASE) == 0 || GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].ptr == nullptr)
            return nullptr;
        else
            return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Scheduling::Thread>()->GetParent()->VMM();
    }

    sl::LinkedList<VMRange> VirtualMemoryManager::InsertRange(NativeUInt base, size_t length, MemoryMapFlags flags)
    {}

    sl::LinkedList<VMRange> VirtualMemoryManager::DestroyRange(NativeUInt base, size_t length)
    {}

    MemoryMapFlags VirtualMemoryManager::MergeFlags(MemoryMapFlags a, MemoryMapFlags b)
    {
        //merge 2 sets of flags in a way that ensures they both get the permissions they need.
    }

    void VirtualMemoryManager::Init()
    {
        pageTables.InitClone();
    }

    PageTableManager& VirtualMemoryManager::PageTables()
    { return pageTables; }

    void VirtualMemoryManager::AddRange(NativeUInt base, size_t length, MemoryMapFlags flags)
    {
        sl::LinkedList<VMRange> damagedRanges = InsertRange(base, length, flags);
        const size_t bitmapLenBytes = (length / PAGE_FRAME_SIZE + 1) / 8 + 1;
        uint8_t* appliedBitmap = reinterpret_cast<uint8_t*>(sl::StackAlloc(bitmapLenBytes));
        sl::memset(appliedBitmap, 0, bitmapLenBytes);

        //now adjust any memory mappings accordingly
        for (auto it = damagedRanges.Begin(); it != damagedRanges.End(); ++it)
        {
            MemoryMapFlags mergedFlags = MergeFlags(it->flags, flags);

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
    {}

    bool VirtualMemoryManager::RemoveRange(NativeUInt base, size_t length)
    {}
}
