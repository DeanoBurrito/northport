#include <boot/LimineBootstrap.h>
#include <boot/LinkerSyms.h>
#include <boot/LimineTags.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <config/DeviceTree.h>
#include <Memory.h>

#if __riscv_xlen == 64
    #include <arch/riscv64/Sbi.h>

    extern char SmpEntry[] asm("SmpEntry");
#endif

#ifdef NP_INCLUDE_LIMINE_BOOTSTRAP
namespace Npk::Boot
{
    constexpr size_t LoaderDataReserveSize = 4 * PageSize;
    uintptr_t loaderDataNext; //next free address
    size_t loaderDataSpace; //bytes remaining for this area
    size_t memmapResponseCapacity;

    struct MemBlock
    {
        uintptr_t base;
        size_t length;
        size_t type;
    };

    uintptr_t DetermineHhdm()
    {
#if __riscv_xlen == 64
        uint64_t satp = ReadCsr("satp");
        satp = (satp >> 60) - 5; //satp now contains the number of page levels
        satp = 1ul << (9 * satp + 11);
        return ~(--satp);
#endif
    }

    void* BootAlloc(size_t t)
    {
        t = sl::AlignUp(t, 16);
        void* ptr = reinterpret_cast<void*>(loaderDataNext);
        loaderDataNext += t;
        loaderDataSpace -= t;
        return ptr;
    }

    void* BootAllocPages(size_t count)
    {
        for (size_t i = 0; i < memmapRequest.response->entry_count; i++)
        {
            auto* entry = memmapRequest.response->entries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;
            if (entry->length < count * PageSize)
                continue;
            
            const uintptr_t addr = entry->base;
            entry->base += count * PageSize;
            entry->length -= count * PageSize;

            if (i > 0 && memmapRequest.response->entries[i - 1]->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
            {
                auto* prevEntry = memmapRequest.response->entries[i - 1];
                prevEntry->length += count * PageSize;
            }
            else
            {
                limine_memmap_entry* newEntry = new(BootAlloc(sizeof(limine_memmap_entry))) limine_memmap_entry{};
                newEntry->base = addr;
                newEntry->length = count * PageSize;
                newEntry->type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
                //TODO: insert into memmap entries are current pos (use capacity then resort).
            }
            return reinterpret_cast<void*>(addr + DetermineHhdm());
        }
        ASSERT_UNREACHABLE();
    }

    limine_memmap_entry* BuildMemoryMap(MemBlock* freeBlocks, MemBlock* reservedBlocks, size_t freeCount, size_t reservedCount, size_t& mmapCount)
    {
        uintptr_t largestBase = 0;
        size_t largestLength = 0;
        size_t entryCount = 0;
        limine_memmap_entry* entries = nullptr;
        bool dryRun = true;

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
                    if (!dryRun)
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

                if (!dryRun)
                    new (&entries[entryCount++]) limine_memmap_entry{ resBlock.base, resBlock.length, resBlock.type };
                else
                    entryCount++;
                base = resBlock.base + resBlock.length;
            }

            const MemBlock& freeBlock = freeBlocks[i];
            if (base < freeBlock.base + freeBlock.length)
            {
                if (!dryRun)
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

        if (dryRun)
        {
            entryCount += 2; //we add one final region below, this can add at max 2 more regions.
            ASSERT(largestLength > LoaderDataReserveSize, "Not enough space for boot shim memory.");

            loaderDataNext = largestBase;
            loaderDataSpace = LoaderDataReserveSize;
            reservedBlocks[reservedCount].base = loaderDataNext;
            reservedBlocks[reservedCount].length = loaderDataSpace;
            reservedBlocks[reservedCount++].type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
            loaderDataNext += DetermineHhdm();
            entries = static_cast<limine_memmap_entry*>(BootAlloc(sizeof(limine_memmap_entry) * entryCount));

            entryCount = 0;
            dryRun = false;
            goto do_build_map;
        }

        mmapCount = entryCount;
        return entries;
    }

    void MemoryMapFromDtb(uintptr_t physBase)
    {
        using namespace Config;

        constexpr size_t MaxMemoryBlocks = 32;
        MemBlock freeBlocks[MaxMemoryBlocks];
        MemBlock reservedBlocks[MaxMemoryBlocks];
        size_t freeCount = 0;
        size_t reservedCount = 2;

        reservedBlocks[0].base = 0; //reserve null page
        reservedBlocks[0].length = PageSize;
        reservedBlocks[0].type = LIMINE_MEMMAP_RESERVED;

        //protect the physical memory where the kernel was loaded
        const size_t kernelTop = physBase + (size_t)KERNEL_BLOB_SIZE;
        reservedBlocks[1].base = sl::AlignDown(physBase, PageSize);
        reservedBlocks[1].length = sl::AlignUp(kernelTop, PageSize) - reservedBlocks[1].base;
        reservedBlocks[1].type = LIMINE_MEMMAP_KERNEL_AND_MODULES;

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
                new (&freeBlocks[freeCount++]) MemBlock{ addrs[i], lengths[i], LIMINE_MEMMAP_USABLE };
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
                    new (&reservedBlocks[reservedCount++]) MemBlock{ addrs[i], lengths[i], LIMINE_MEMMAP_RESERVED };
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
            if (mmapEntries[i].type != LIMINE_MEMMAP_USABLE || mmapEntries[i].type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE)
                continue;
            
            limine_memmap_entry& entry = mmapEntries[i];
            const uint64_t top = entry.base + entry.length;
            entry.base = sl::AlignUp(entry.base, PageSize);
            entry.length = sl::AlignDown(top - entry.base, 0x1000);
        }

        //gives us room for future expansion later on
        memmapResponseCapacity = mmapCount * 2;

        //populate the memory map request
        auto* mmapResponse = new(BootAlloc(sizeof(limine_memmap_response))) limine_memmap_response{};
        limine_memmap_entry** entryPtrs = static_cast<limine_memmap_entry**>(BootAlloc(sizeof(void*) * memmapResponseCapacity));

        mmapResponse->entry_count = mmapCount;
        mmapResponse->entries = entryPtrs;
        for (size_t i = 0; i < mmapCount; i++)
            entryPtrs[i] = &mmapEntries[i];
        memmapRequest.response = mmapResponse;
    }

    struct SmpConfig
    {
        uintptr_t stack;
        uintptr_t ptRoot;
        void* smpInfoStruct;
    };

    void PopulateSmpResponse(size_t bspId, uintptr_t virtOffset)
    {
        using namespace Config;
        auto cpusNode = DeviceTree::Global().GetNode("/cpus");
        if (!cpusNode)
            return;

        auto* smpResponse = new(BootAlloc(sizeof(limine_smp_response))) limine_smp_response{};
        smpResponse->bsp_hart_id = bspId;
        bool dryRun = true;

    do_cpu_search:
        size_t cpuCount = 0;
        for (size_t i = 0; i < cpusNode->childCount; i++)
        {
            auto child = *DeviceTree::Global().GetChild(*cpusNode, i);
            auto compatProp = child.GetProp("compatible");
            if (!compatProp)
                continue;
            const char* compatStr = compatProp->ReadStr();
            const size_t compatStrLen = sl::memfirst(compatStr, 0, 6);
            if (compatStrLen != 5 || sl::memcmp("riscv", compatStr, compatStrLen) != 0)
                continue;
            
            if (dryRun)
            {
                cpuCount++;
                continue;
            }

            limine_smp_info* cpu = new(BootAlloc(sizeof(limine_smp_info))) limine_smp_info{};
            smpResponse->cpus[cpuCount++] = cpu;
            cpu->goto_address = 0;
            cpu->extra_argument = 0;
            cpu->plic_context = 0; //TODO: determine plic context id
            cpu->reserved = 0;

            auto regProp = child.GetProp("reg");
            ASSERT(regProp, "/cpus/cpuX has no reg property.")
            cpu->hart_id = regProp->ReadNumber();

#if __riscv_xlen == 64
            SmpConfig* config = new(BootAlloc(sizeof(SmpConfig))) SmpConfig{};
            config->smpInfoStruct = cpu;
            config->ptRoot = ReadCsr("satp");
            config->stack = (uintptr_t)BootAllocPages(2) + 2 * PageSize;

            SbiStartHart(cpu->hart_id, (uintptr_t)SmpEntry - virtOffset, (uintptr_t)config - DetermineHhdm());
#endif
        }

        if (!dryRun)
        {
            smpRequest.response = smpResponse;
            return;
        }
        
        smpResponse->cpu_count = cpuCount;
        smpResponse->cpus = static_cast<limine_smp_info**>(BootAlloc(sizeof(void*) * cpuCount));
        dryRun = false;
        goto do_cpu_search;
    }
    
    void PerformLimineBootstrap(uintptr_t physLoadBase, uintptr_t virtLoadBase, size_t bspId, uintptr_t dtb)
    {
        Config::DeviceTree::Global().Init(dtb);
        MemoryMapFromDtb(physLoadBase);

        auto* infoResponse = new(BootAlloc(sizeof(limine_bootloader_info_response))) limine_bootloader_info_response{};
        infoResponse->name = (char*)"Northport Boot Shim";
        infoResponse->version = (char*)"1.0.0";
        bootloaderInfoRequest.response = infoResponse;

        auto* hhdmResponse = new(BootAlloc(sizeof(limine_hhdm_response))) limine_hhdm_response{};
        hhdmResponse->offset = DetermineHhdm();
        hhdmRequest.response = hhdmResponse;

        auto* kernelAddrResponse = new(BootAlloc(sizeof(limine_kernel_address_response))) limine_kernel_address_response{};
        kernelAddrResponse->physical_base = physLoadBase;
        kernelAddrResponse->virtual_base = virtLoadBase;
        kernelAddrRequest.response = kernelAddrResponse;

        auto* dtbResponse = new(BootAlloc(sizeof(limine_dtb_response))) limine_dtb_response{};
        dtbResponse->dtb_ptr = (void*)(dtb + DetermineHhdm());
        dtbRequest.response = dtbResponse;

        PopulateSmpResponse(bspId, virtLoadBase - physLoadBase);

        Log("Boot shim finished, reclaim wastage: 0x%lx bytes.", LogLevel::Info, loaderDataSpace);
    }
}
#endif
