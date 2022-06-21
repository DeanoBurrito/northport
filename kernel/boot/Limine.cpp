#include <boot/Limine.h>
#include <memory/PhysicalMemory.h>
#include <memory/KernelHeap.h>
#include <Log.h>

namespace Kernel::Boot
{
    [[gnu::used]]
    limine_bootloader_info_request bootloaderInfoRequest
    {
        .id = LIMINE_BOOTLOADER_INFO_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_stack_size_request stackSizeRequest
    {
        .id = LIMINE_STACK_SIZE_REQUEST,
        .revision = 0,
        .response = nullptr,
        .stack_size = 0x2000
    };

    [[gnu::used]]
    limine_hhdm_request hhdmRequest
    {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = nullptr
    };
    
    [[gnu::used]]
    limine_framebuffer_request framebufferRequest
    {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_5_level_paging_request la57PagingRequest
    {
        .id = LIMINE_5_LEVEL_PAGING_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_memmap_request memmapRequest
    {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_kernel_file_request kernelFileRequest
    {
        .id = LIMINE_KERNEL_FILE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_module_request modulesRequest
    {
        .id = LIMINE_MODULE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_rsdp_request rsdpRequest
    {
        .id = LIMINE_RSDP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_efi_system_table_request efiTableRequest
    {
        .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_boot_time_request bootTimeRequest
    {
        .id = LIMINE_BOOT_TIME_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    [[gnu::used]]
    limine_kernel_address_request kernelAddrRequest
    {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    void PrintBootInfo()
    {
        using namespace Kernel;
        Log("Bootloader provided info:", LogSeverity::Verbose);
        
        if (bootloaderInfoRequest.response == nullptr)
        {
            Log("Bootloader ignored general info request, cannot provide name and version.", LogSeverity::Warning);
        }
        else
        {
            Logf("Loaded by: %s, v%s", LogSeverity::Verbose, 
                bootloaderInfoRequest.response->name, bootloaderInfoRequest.response->version);
        }

        if (stackSizeRequest.response != nullptr)
            Logf("Bootloader accepted override stack size of 0x%x bytes.", LogSeverity::Verbose, stackSizeRequest.stack_size);
        if (hhdmRequest.response != nullptr)
            Logf("HHDM set to 0x%lx", LogSeverity::Verbose, hhdmRequest.response->offset);
        if (framebufferRequest.response != nullptr)
        {
            auto fb = framebufferRequest.response;
            for (unsigned i = 0; i < fb->framebuffer_count; i++)
            {
                Logf("Boot framebuffer %u: w=%u, h=%u, bpp=%u, base=0x%lx", LogSeverity::Verbose,
                    i, fb->framebuffers[i]->width, fb->framebuffers[i]->height, fb->framebuffers[i]->bpp, (uint64_t)fb->framebuffers[i]->address);

            }
        }
        if (la57PagingRequest.response != nullptr)
            Log("5-level paging was enabled.", LogSeverity::Verbose);

        if (memmapRequest.response != nullptr)
        {
            auto mmap = memmapRequest.response;
            Logf("Memory map has %u entries:", LogSeverity::Verbose, mmap->entry_count);

            for (unsigned i = 0; i < mmap->entry_count; i++)
            {
                const char* typeStr = "unknown";
                switch (mmap->entries[i]->type)
                {
                    //thanks, I hate it.
                    case LIMINE_MEMMAP_USABLE: 
                        typeStr = "usable"; break;
                    case LIMINE_MEMMAP_RESERVED: 
                        typeStr = "reserved"; break;
                    case LIMINE_MEMMAP_ACPI_RECLAIMABLE: 
                        typeStr = "acpi reclaim"; break;
                    case LIMINE_MEMMAP_ACPI_NVS: 
                        typeStr = "acpi nvs"; break;
                    case LIMINE_MEMMAP_BAD_MEMORY: 
                        typeStr = "bad"; break;
                    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: 
                        typeStr = "bootloader reclaim"; break;
                    case LIMINE_MEMMAP_KERNEL_AND_MODULES: 
                        typeStr = "kernel/modules"; break;
                    case LIMINE_MEMMAP_FRAMEBUFFER: 
                        typeStr = "boot framebuffer"; break;
                }
                
                Logf("Memory region %u: base=0x%0lx, length=%lx, type=%s", LogSeverity::Verbose, 
                    i, mmap->entries[i]->base, mmap->entries[i]->length, typeStr);
            }
        }

        if (kernelFileRequest.response != nullptr)
        {
            Logf("Kernel file located at addr=0x%lx, size=0x%lx", LogSeverity::Verbose,
                (uint64_t)kernelFileRequest.response->kernel_file->address, kernelFileRequest.response->kernel_file->size);
        }

        if (modulesRequest.response != nullptr)
        {
            Logf("%u modules passed to kernel.", LogSeverity::Verbose, modulesRequest.response->module_count);

            auto mods = modulesRequest.response->modules;
            for (unsigned i = 0; i < modulesRequest.response->module_count; i++)
            {
                Logf("Module %u: addr=0x%lx size=0x%lx, cmdline=%s", LogSeverity::Verbose,
                    i, (uint64_t)mods[i]->address, mods[i]->size, mods[i]->cmdline);
            }
        }

        if (rsdpRequest.response != nullptr)
            Logf("ACPI RSDP found at 0x%lx", LogSeverity::Verbose, (uint64_t)rsdpRequest.response->address);
        if (efiTableRequest.response == nullptr)
            Log("No EFI system table provided.", LogSeverity::Verbose);
        else
            Logf("EFI system table at 0x%lx", LogSeverity::Verbose, (uint64_t)efiTableRequest.response->address);
        if (bootTimeRequest.response != nullptr)
            Logf("Bootloader epoch: %i", LogSeverity::Verbose, bootTimeRequest.response->boot_time);
        if (kernelAddrRequest.response != nullptr)
        {
            Logf("Kernel load mapping: virtualBase=0x%lx, physicalBase=0x%lx", LogSeverity::Verbose,
            kernelAddrRequest.response->virtual_base, kernelAddrRequest.response->physical_base);
        }

        Log("End of bootloader info.", LogSeverity::Verbose);

        auto pmmStats = Memory::PMM::Global()->GetStats();
        Logf("Usable physical memory: %U (%u bytes)", LogSeverity::Verbose, 
            pmmStats.totalPages * pmmStats.pageSizeInBytes, pmmStats.totalPages * pmmStats.pageSizeInBytes);
        Logf("Physical memory breakdown: %U/%U in use, %U reserved, %U reclaimable, %U in use by kernel.", LogSeverity::Verbose, 
            pmmStats.usedPages * pmmStats.pageSizeInBytes, pmmStats.totalPages * pmmStats.pageSizeInBytes,
            pmmStats.reservedBytes, pmmStats.reclaimablePages * pmmStats.pageSizeInBytes, pmmStats.kernelPages * pmmStats.pageSizeInBytes);

        Memory::HeapMemoryStats heapStats;
        Memory::KernelHeap::Global()->GetStats(heapStats);
        Logf("KHeap init stats: slabsBase=0x%lx, poolBase=0x%lx", LogSeverity::Verbose, 
            heapStats.slabsGlobalBase.raw, heapStats.slabsGlobalBase.raw + Memory::KernelHeapPoolOffset);
        Logf("Pool stats: %U/%U used, nodeCount=%u", LogSeverity::Verbose, 
            heapStats.poolStats.usedBytes, heapStats.poolStats.totalSizeBytes, heapStats.poolStats.nodes);
        for (size_t i = 0; i < heapStats.slabCount; i++)
        {
            Logf("Slab %u stats: size=%u bytes, base=0x%lx, usage %u/%u slabs, segments=%u", LogSeverity::Verbose,
                i, heapStats.slabStats[i].slabSize, heapStats.slabStats[i].base.raw, 
                heapStats.slabStats[i].usedSlabs, heapStats.slabStats[i].totalSlabs,
                heapStats.slabStats[i].segments);
        }

        Log("End of memory info.", LogSeverity::Verbose);
    }
}
