#include <memory/Vmm.h>
#include <memory/Pmm.h>
#include <memory/Heap.h>
#include <arch/Platform.h>
#include <arch/Paging.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <Memory.h>

namespace Npk::Memory
{
    constexpr PageFlags ConvertFlags(VMFlags flags)
    {
        /*  VMFlags are stable across platforms, while PageFlags have different meanings
            depending on the ISA. This provides source-level translation between the two. */
        PageFlags value = PageFlags::None;
        if (flags & VMFlags::Writable)
            value |= PageFlags::Write;
        if (flags & VMFlags::Executable)
            value |= PageFlags::Execute;
        if (flags & VMFlags::User)
            value |= PageFlags::User;
        
        return value;
    }
    
    bool VirtualMemoryManager::InsertRange(const VMRange& range)
    {
        if (range.length < PageSize)
            return false;

        size_t index = 0;
        while (index < ranges.Size() && ranges[index].base < range.base)
            index++;

        if (index == ranges.Size())
        {
            if (ranges.Size() > 0 && ranges.Back().Top() > range.base)
                return false;
            ranges.EmplaceBack(range);
            return true;
        }
        
        if (range.Top() > ranges[index].base)
            return false;
        
        ranges.Emplace(index, range);
        return true;
    }

    //kernel vmm is lazy-initialized, this horror just reserves enough memory for it.
    alignas(VirtualMemoryManager) uint8_t kernelVmm[sizeof(VirtualMemoryManager)];
    void VirtualMemoryManager::SetupKernel()
    {
        new (kernelVmm) VirtualMemoryManager(VmmKey{});
        Heap::Global().Init();
    }
    
    VirtualMemoryManager& VirtualMemoryManager::Kernel()
    { return *reinterpret_cast<VirtualMemoryManager*>(kernelVmm); }

    VirtualMemoryManager& VirtualMemoryManager::Current()
    { return *static_cast<VirtualMemoryManager*>(CoreLocal().vmm); }

    VirtualMemoryManager::VirtualMemoryManager()
    {
        allocLowerLimit = PageSize; //dont alloc within first (null) page.
        allocUpperLimit = ~hhdmBase;
        Log("User VMM initialized: ptRoot=0x%lx", LogLevel::Info, (uintptr_t)ptRoot);
    }

    VirtualMemoryManager::VirtualMemoryManager(VmmKey)
    {
        sl::ScopedLock scopeLock(lock);

        //protect the hhdm and an area reserved for the heap from allocations
        allocLowerLimit = hhdmBase + HeapLimit + HhdmLimit;
        allocUpperLimit = sl::AlignDown((uintptr_t)KERNEL_BLOB_BEGIN, GiB);

        PagingSetup(); //platform specific paging setup

        kernelMasterTables = ptRoot = PMM::Global().Alloc();
        sl::memset(kernelMasterTables, 0, PageSize);

        //map the HHDM, dont use pages bigger than 1GiB in size.
        const PageSizes hhdmSize = MaxSupportedPagingSize() > PageSizes::_1G ? PageSizes::_1G : MaxSupportedPagingSize();
        const uintptr_t hhdmPageSize = GetPageSize(hhdmSize);

        //map hhdm
        for (uintptr_t i = 0; i < hhdmLength; i += hhdmPageSize)
            MapMemory(ptRoot, i + hhdmBase, i, PageFlags::Write, hhdmSize, false);
        
        auto MapSection = [&](uintptr_t addr, size_t length, PageFlags flags)
        {
            const auto* resp = Boot::kernelAddrRequest.response;
            length = sl::AlignUp(addr + length, PageSize) - addr;
            addr = sl::AlignDown(addr, PageSize);
            
            for (uintptr_t i = addr; i < addr + length; i += PageSize)
                MapMemory(ptRoot, i, i - resp->virtual_base + resp->physical_base, flags, PageSizes::_4K, false);
        };

        //map kernel
        MapSection((uintptr_t)KERNEL_TEXT_BEGIN, (size_t)KERNEL_TEXT_SIZE, PageFlags::Execute);
        MapSection((uintptr_t)KERNEL_RODATA_BEGIN, (size_t)KERNEL_RODATA_SIZE, PageFlags::None);
        MapSection((uintptr_t)KERNEL_DATA_BEGIN, (size_t)KERNEL_DATA_SIZE, PageFlags::Write);

        LoadTables(ptRoot);

        Log("Kernel VMM initiailized: ptRoot=0x%lx, allocSpace=0x%lx - 0x%lx", LogLevel::Info, 
            (uintptr_t)ptRoot, allocLowerLimit, allocUpperLimit);
    }

    bool VirtualMemoryManager::AddRange(const VMRange& range)
    {
        if (range.Top() > allocUpperLimit || range.base < allocLowerLimit)
            return false;
        
        sl::ScopedLock scopeLock(lock);

        if (!InsertRange(range))
            return false;
        
        //TODO: demand paging
        for (uintptr_t scan = 0; scan < range.length; scan += PageSize)
            MapMemory(ptRoot, range.base + scan, (uintptr_t)PMM::Global().Alloc(), ConvertFlags(range.flags), PageSizes::_4K, false);
        
        if (ptRoot == kernelMasterTables)
            kernelTablesGen++;
        return true;
    }

    bool VirtualMemoryManager::RemoveRange(const VMRange& range)
    {
        sl::ScopedLock scopeLock(lock);

        size_t found = ranges.Size();
        for (size_t i = 0; i < ranges.Size(); i++)
        {
            if (ranges[i].base != range.base || ranges[i].length != range.length)
                continue; //TODO: better search criteria, what about removing part of a range?
            
            found = i;
            break;
        }

        if (found == ranges.Size())
            return false;
        
        uintptr_t freePage = ranges[found].base;
        while (freePage < ranges[found].Top())
        {
            PageSizes sizeUsed;
            uintptr_t physAddr;

            if (UnmapMemory(ptRoot, freePage, physAddr, sizeUsed, true))
            {
                PMM::Global().Free((void*)physAddr, GetPageSize(sizeUsed) / PageSize);
                freePage += GetPageSize(sizeUsed);
            }
            else
                freePage += PageSize;
        }

        if (ptRoot == kernelMasterTables)
            kernelTablesGen++;
        return true;
    }

    sl::Opt<VMRange> VirtualMemoryManager::AllocRange(size_t length, VMFlags flags, uintptr_t lowerBound, uintptr_t upperBound)
    {
        return AllocRange(length, 0, flags, lowerBound, upperBound);
    }

    sl::Opt<VMRange> VirtualMemoryManager::AllocRange(size_t length, uintptr_t physBase, VMFlags flags, uintptr_t lowerBound, uintptr_t upperBound)
    {
        lowerBound = sl::Max(lowerBound, allocLowerLimit);
        upperBound = sl::Min(upperBound, allocUpperLimit);
        length = sl::AlignUp(length, PageSize);
        
        sl::ScopedLock scopeLock(lock);

        uintptr_t check = lowerBound;
        if (ranges.Size() == 0 || check + length <= ranges[0].base)
            goto alloc_range_success;
        
        for (size_t i = 0; i < ranges.Size(); i++)
        {
            if (ranges[i].Top() < lowerBound)
                continue;
            
            check = ranges[i].Top();
            if (i + 1 == ranges.Size())
                break;
            
            if (check + length > upperBound)
                return {};
            if (check + length <= ranges[i + 1].base)
                goto alloc_range_success;
        }

        check = sl::Max(ranges.Back().Top(), lowerBound);
        if (check + length > upperBound)
            return {};
        else
            goto alloc_range_success;
        
    alloc_range_success:
        VMRange range { check, length, flags };
        if (!InsertRange(range))
            return {};
        
        //TODO: demand paging, also do we need to use 4K pages for everything?
        const PageFlags pFlags = ConvertFlags(range.flags);
        for (size_t i = 0; i < length; i += PageSize)
        {
            if (physBase == 0)
                MapMemory(ptRoot, range.base + i, (uintptr_t)PMM::Global().Alloc(), pFlags, PageSizes::_4K, false);
            else
                MapMemory(ptRoot, range.base + i, physBase + i, pFlags, PageSizes::_4K, false);
        }

        return range;
    }

    bool VirtualMemoryManager::RangeExists(const VMRange& range, bool checkFlags) const
    {
        for (size_t i = 0; i < ranges.Size(); i++)
        {
            if (ranges[i].base > range.Top())
                return false;
            if (range.base >= ranges[i].base && range.Top() <= ranges[i].Top())
                return checkFlags ? (range.flags & ranges[i].flags) == range.flags : true;
        }

        return false;
    }
}
