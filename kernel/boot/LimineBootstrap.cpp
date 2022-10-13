#include <boot/LimineBootstrap.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <devices/DeviceTree.h>
#include <Memory.h>

#ifdef NP_INCLUDE_LIMINE_BOOTSTRAP
namespace Npk::Boot
{
    constexpr size_t LoaderDataReserveSize = 4 * PageSize;
    uintptr_t loaderDataNext; //next free address
    size_t loaderDataSpace; //bytes remaining for this area

    struct MemBlock
    {
        uintptr_t base;
        size_t length;
    };

    uintptr_t DetermineHhdm()
    {
#if __riscv_xlen == 64
        uint64_t satp;
        asm volatile("csrr %0, satp" : "=r"(satp) :: "memory");
        satp = (satp >> 60) - 5; //satp now contains the number of page levels
        satp = 1ul << (9 * satp + 11);
        return ~(--satp);
#endif
    }

    void* BootstrapAlloc(size_t t)
    {
        t = sl::AlignUp(t, 16);
        void* ptr = reinterpret_cast<void*>(loaderDataNext);
        loaderDataNext += t;
        loaderDataSpace -= t;
        return ptr;
    }

    limine_memmap_entry* BuildMemoryMap(MemBlock* freeBlocks, MemBlock* reservedBlocks, size_t freeCount, size_t reservedCount, size_t& mmapCount)
    {
        uintptr_t largestBase = 0;
        size_t largestLength = 0;
        size_t entryCount = 0;
        limine_memmap_entry* entries = nullptr;
        bool testOnly = true;

    do_build_map:
        for (size_t i = 0; i < freeCount; i++)
        {
            uintptr_t base = freeBlocks[i].base;
            for (size_t j = 0; j < reservedCount; j++)
            {
                const MemBlock& resBlock = reservedBlocks[j];
                if (base + freeBlocks[i].length < resBlock.base)
                    break;
                if (base > resBlock.base + resBlock.length)
                    continue;
                
                if (resBlock.base - base > 0)
                {
                    if (!testOnly)
                        new (&entries[entryCount++]) limine_memmap_entry{ base, resBlock.base - base, LIMINE_MEMMAP_USABLE };
                    else
                    {
                        entryCount++;
                        if (resBlock.base - base > largestLength)
                        {
                            largestLength = resBlock.base - base;
                            largestBase = base;
                        }
                    }
                }

                if (!testOnly)
                    new (&entries[entryCount++]) limine_memmap_entry{ resBlock.base, resBlock.length, LIMINE_MEMMAP_RESERVED };
                else
                    entryCount++;
                base = resBlock.base + resBlock.length;
            }

            const MemBlock& freeBlock = freeBlocks[i];
            if (base < freeBlock.base + freeBlock.length)
            {
                if (!testOnly)
                    new (&entries[entryCount++]) limine_memmap_entry{ base, freeBlock.base + freeBlock.length - base, LIMINE_MEMMAP_USABLE };
                else
                {
                    entryCount++;
                    if (freeBlock.base + freeBlock.length - base > largestLength)
                    {
                        largestLength = freeBlock.base + freeBlock.length - base;
                        largestBase = base;
                    }
                }
            }
        }

        if (testOnly)
        {
            //we're going to insert an extra reserved entry, which may result in a max of 2 more entries.
            entryCount += 2;

            //we reserve space for the memory, as well as some extra space for other tags
            const size_t claimedBytes = LoaderDataReserveSize + sl::AlignUp(entryCount * sizeof(limine_memmap_entry), PageSize);
            ASSERT(largestLength > claimedBytes, "Not enough space for boot shim memory.");

            loaderDataNext = largestBase + DetermineHhdm();
            loaderDataSpace = claimedBytes;
            reservedBlocks[reservedCount].base = loaderDataNext;
            reservedBlocks[reservedCount++].length = loaderDataSpace;
            entries = static_cast<limine_memmap_entry*>(BootstrapAlloc(sizeof(limine_memmap_entry) * entryCount));

            entryCount = 0;
            testOnly = false;
            goto do_build_map;
        }

        mmapCount = entryCount;
        return entries;
    }

    void MemoryMapFromDtb(uintptr_t physBase)
    {
        using namespace Devices;

        constexpr size_t MaxMemoryBlocks = 32;
        MemBlock freeBlocks[MaxMemoryBlocks];
        MemBlock reservedBlocks[MaxMemoryBlocks];
        size_t freeCount = 0;
        size_t reservedCount = 2;

        reservedBlocks[0].base = 0; //reserve null page
        reservedBlocks[0].length = PageSize;

        //pprotect the physical memory where the kernel is loaded. Technically
        //this should be marked as kernel, not reserved, as this breaks spec.
        //However, we are fine until mint sees this.
        reservedBlocks[1].base = physBase;
        reservedBlocks[1].length = (size_t)KERNEL_BLOB_SIZE;

        //find all free physical memory
        {
            auto memoryNode = DeviceTree::Global().GetNode("/memory");
            ASSERT(memoryNode.HasValue(), "No '/memory' node in device tree");
            auto memoryRegs = memoryNode->GetProp("reg");
            ASSERT(memoryRegs.HasValue(), "DTB memory node has no reg property");

            const size_t regsCount = memoryRegs->ReadRegs(*memoryNode, nullptr, nullptr);
            uintptr_t addrs[regsCount];
            size_t lengths[regsCount];
            memoryRegs->ReadRegs(*memoryNode, addrs, lengths);

            for (size_t i = 0; i < regsCount; i++)
                new (&freeBlocks[freeCount++]) MemBlock{ addrs[i], lengths[i] };
        }

        //find any reserved regions
        {
            auto reservedNode = DeviceTree::Global().GetNode("/reserved-memory");
            ASSERT(reservedNode.HasValue(), "DTB has no '/reserved-memory' node.");

            for (size_t i = 0; i < reservedNode->childCount; i++)
            {
                DtNode child = *DeviceTree::Global().GetChild(*reservedNode, i);

                //TODO: we should also support unit address + size property
                auto regsProp = child.GetProp("reg");
                ASSERT(regsProp.HasValue(), "reserved memory node has not 'regs' property.");
                const size_t regsCount = regsProp->ReadRegs(child, nullptr, nullptr);
                uintptr_t addrs[regsCount];
                size_t lengths[regsCount];
                regsProp->ReadRegs(child, addrs, lengths);

                for (size_t i = 0; i < regsCount; i++)
                    new (&reservedBlocks[reservedCount++]) MemBlock{ addrs[i], lengths[i] };
            }
        }

        //sort both lists by their base address (bubble sort).
        for (size_t i = 0; i < freeCount - 1; i++)
        {
            for (size_t j = 0; j < freeCount - i - 1; j++)
            {
                if (freeBlocks[j].base > freeBlocks[j + 1].base)
                    sl::Swap(freeBlocks[j], freeBlocks[j + 1]);
            }
        }
        for (size_t i = 0; i < reservedCount - 1; i++)
        {
            for (size_t j = 0; j < reservedCount - i - 1; j++)
            {
                if (reservedBlocks[j].base > reservedBlocks[j + 1].base)
                    sl::Swap(reservedBlocks[j], reservedBlocks[j + 1]);
            }
        }

        size_t mmapCount = 0;
        limine_memmap_entry* mmapEntries = BuildMemoryMap(freeBlocks, reservedBlocks, freeCount, reservedCount, mmapCount);
        if (PageSize != 0x1000)
            Log("Unusual native page size: 0x%lu", LogLevel::Warning, PageSize);

        //align usable entries to 4K
        for (size_t i = 0; i < mmapCount; i++)
        {
            if (mmapEntries[mmapCount].type != LIMINE_MEMMAP_USABLE)
                continue;
            
            limine_memmap_entry& entry = mmapEntries[i];
            const uint64_t top = entry.base + entry.length;
            entry.base = sl::AlignUp(entry.base, PageSize);
            entry.length = sl::AlignDown(top - entry.base, 0x1000);
        }

        //populate the memory map request
        auto* mmapResponse = new(BootstrapAlloc(sizeof(limine_memmap_response))) limine_memmap_response{};
        limine_memmap_entry** entryPtrs = static_cast<limine_memmap_entry**>(BootstrapAlloc(sizeof(void*) * mmapCount));

        mmapResponse->entry_count = mmapCount;
        mmapResponse->entries = entryPtrs;
        for (size_t i = 0; i < mmapCount; i++)
            entryPtrs[i] = &mmapEntries[i];
        memmapRequest.response = mmapResponse;
    }
    
    void PerformLimineBootstrap(uintptr_t physLoadBase, uintptr_t virtLoadBase, size_t bspId, uintptr_t dtb)
    {
        Devices::DeviceTree::Global().Init(dtb);
        MemoryMapFromDtb(physLoadBase);

        auto* infoResponse = new(BootstrapAlloc(sizeof(limine_bootloader_info_response))) limine_bootloader_info_response{};
        infoResponse->version = (char*)"Northport Boot Shim";
        infoResponse->name = (char*)"1.0.0";
        bootloaderInfoRequest.response = infoResponse;

        auto* hhdmResponse = new(BootstrapAlloc(sizeof(limine_hhdm_response))) limine_hhdm_response{};
        hhdmResponse->offset = DetermineHhdm();
        hhdmRequest.response = hhdmResponse;

        auto* kernelAddrResponse = new(BootstrapAlloc(sizeof(limine_kernel_address_response))) limine_kernel_address_response{};
        kernelAddrResponse->physical_base = physLoadBase;
        kernelAddrResponse->virtual_base = virtLoadBase;
        kernelAddrRequest.response = kernelAddrResponse;

        auto* dtbResponse = new(BootstrapAlloc(sizeof(limine_dtb_response))) limine_dtb_response{};
        dtbResponse->dtb_ptr = (void*)(dtb + DetermineHhdm());
        dtbRequest.response = dtbResponse;

        Log("Boot shim finished, reclaim wastage: 0x%lx bytes.", LogLevel::Info, loaderDataSpace);
    }
}
#endif
