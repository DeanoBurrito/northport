#include <boot/LimineTags.h>
#include <config/ConfigStore.h>
#include <debug/Log.h>
#include <UnitConverter.h>

namespace Npk::Boot
{
    alignas(8) 
    limine_bootloader_info_request bootloaderInfoRequest
    {
        .id = LIMINE_BOOTLOADER_INFO_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_stack_size_request stackSizeRequest
    {
        .id = LIMINE_STACK_SIZE_REQUEST,
        .revision = 0,
        .response = nullptr,
        .stack_size = 0x2000
    };

    alignas(8)
    limine_hhdm_request hhdmRequest
    {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = nullptr
    };
    
    alignas(8)
    limine_framebuffer_request framebufferRequest
    {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_paging_mode_request pagingModeRequest
    {
        .id = LIMINE_PAGING_MODE_REQUEST,
        .revision = 0,
        .response = nullptr,
        .mode = LIMINE_PAGING_MODE_MAX,
        .flags = 0,
    };

    alignas(8)
    limine_memmap_request memmapRequest
    {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_module_request modulesRequest
    {
        .id = LIMINE_MODULE_REQUEST,
        .revision = 1,
        .response = nullptr,
        .internal_module_count = 0,
        .internal_modules = nullptr
    };

    alignas(8)
    limine_rsdp_request rsdpRequest
    {
        .id = LIMINE_RSDP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_efi_system_table_request efiTableRequest
    {
        .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_boot_time_request bootTimeRequest
    {
        .id = LIMINE_BOOT_TIME_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_kernel_address_request kernelAddrRequest
    {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_dtb_request dtbRequest
    {
        .id = LIMINE_DTB_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    alignas(8)
    limine_smp_request smpRequest
    {
        .id = LIMINE_SMP_REQUEST,
        .revision = 0,
        .response = nullptr,
        .flags = 0
    };

    alignas(8)
    limine_kernel_file_request kernelFileRequest
    {
        .id = LIMINE_KERNEL_FILE_REQUEST,
        .revision = 0,
        .response = nullptr,
    };

    [[gnu::used, gnu::section(".limine_reqs")]]
    void* requests[] = 
    {
        &bootloaderInfoRequest,
        &stackSizeRequest,
        &hhdmRequest,
        &framebufferRequest,
        &pagingModeRequest,
        &memmapRequest,
        &modulesRequest,
        &rsdpRequest,
        &efiTableRequest,
        &bootTimeRequest,
        &kernelAddrRequest,
        &dtbRequest,
        &smpRequest,
        &kernelFileRequest,
        nullptr
    };

    constexpr const char* RequestNameStrs[] =
    {
        "Bootloader info",
        "Stack size",
        "HHDM",
        "Framebuffer",
        "Paging mode",
        "Memory map",
        "Modules",
        "RSDP",
        "EFI runtime table",
        "Boot time",
        "Kernel address",
        "DTB",
        "SMP",
        "Kernel file",
    };

    //I'll admit it, I am a little vain.
    using RequestPrinterFunc = void (*)();
    constexpr RequestPrinterFunc RequestPrinters[] =
    {
        [] () 
        {
            auto resp = bootloaderInfoRequest.response;
            Log("  name=%s, ver=%s", LogLevel::Verbose, resp->name, resp->version);
        },
        nullptr,
        [] ()
        {
            auto resp = hhdmRequest.response;
            Log("  offset=0x%" PRIx64, LogLevel::Verbose, resp->offset);
        },
        [] ()
        {
            auto resp = framebufferRequest.response;
            for (size_t i = 0; i < resp->framebuffer_count; i++)
            {
                auto fb = resp->framebuffers[i];
                Log("  fb %zu: addr=%p, w=%" PRIu64", h=%" PRIu64", s=%" PRIu64", bpp=%u, red=%u<<%u, green=%u<<%u, blue=%u<<%u", 
                    LogLevel::Verbose, i, fb->address, fb->width, fb->height, fb->pitch, fb->bpp,
                    fb->red_mask_size, fb->red_mask_shift, fb->green_mask_size, fb->green_mask_shift,
                    fb->blue_mask_size, fb->blue_mask_shift);
            }
        },
        [] ()
        {
            auto resp = pagingModeRequest.response;
            Log("  mode=%" PRIu64", flags=0x%" PRIx64, LogLevel::Verbose, resp->mode, resp->flags);
        },
        [] ()
        {
            constexpr const char* EntryTypeStrs[] =
            {
                "usable", "reserved", "acpi reclaimable", "acpi nvs", "bad memory",
                "bootloader reclaimable", "kernel/modules", "framebuffer"
            };

            auto resp = memmapRequest.response;
            for (size_t i = 0; i < resp->entry_count; i++)
            {
                auto entry = resp->entries[i];
                auto conv = sl::ConvertUnits(entry->length, sl::UnitBase::Binary);
                Log("  %zu: base=0x%" PRIx64", length=0x%" PRIx64" (%zu.%zu %sB), type=%s", LogLevel::Verbose, i, 
                    entry->base, entry->length, conv.major, conv.minor, conv.prefix, EntryTypeStrs[entry->type]);
            }
        },
        [] ()
        {
            auto resp = modulesRequest.response;
            for (size_t i = 0; i < resp->module_count; i++)
            {
                auto mod = resp->modules[i];
                auto conv = sl::ConvertUnits(mod->size, sl::UnitBase::Decimal);
                Log("  %zu: addr=%p, size=0x%" PRIx64" (%zu.%zu %sB), path=%s, cmdline=%s", LogLevel::Verbose,
                    i, mod->address, mod->size, conv.major, conv.minor, conv.prefix, mod->path, mod->cmdline);
            }
        },
        [] ()
        {
            Log("  address=%p", LogLevel::Verbose, rsdpRequest.response->address);
        },
        [] ()
        {
            Log("  address=%p", LogLevel::Verbose, efiTableRequest.response->address);
        },
        [] ()
        {
            Log("  epoch=%" PRIi64, LogLevel::Verbose, bootTimeRequest.response->boot_time);
        },
        [] ()
        {
            auto resp = kernelAddrRequest.response;
            Log("  phys=0x%" PRIx64", virt=0x%" PRIx64, LogLevel::Verbose, resp->physical_base, resp->virtual_base);
        },
        [] ()
        {
            Log("  address=%p", LogLevel::Verbose, dtbRequest.response->dtb_ptr);
        },
        [] ()
        {
            auto resp = smpRequest.response;
            
            for (size_t i = 0; i < resp->cpu_count; i++)
            {
#if defined(__x86_64__)
                const size_t localId = resp->cpus[i]->lapic_id;
                const bool isBsp = localId == resp->bsp_lapic_id;
#elif defined(__riscv)
                const size_t localId = resp->cpus[i]->hartid;
                const size_t isBsp = localId == resp->bsp_hartid;
#elif defined(__m68k__)
                const size_t localId = resp->cpus[i]->id;
                const size_t isBsp = true;
#endif
                Log("  %zu: acpiId=%" PRIu32", gotoAddr=%p%s", LogLevel::Verbose, localId, 
                    resp->cpus[i]->processor_id, &resp->cpus[i]->goto_address, isBsp ? ", bsp" : "");
            }
        },
        [] ()
        {
            auto file = kernelFileRequest.response->kernel_file;
            auto conv = sl::ConvertUnits(file->size, sl::UnitBase::Decimal);
            Log("  addr=%p, size=0x%" PRIx64" (%zu.%zu %sB), path=%s, cmdline=%s", LogLevel::Verbose,
                file->address, file->size, conv.major, conv.minor, conv.prefix, file->path, file->cmdline);
        },
    };

    struct LimineReq
    {
        uint64_t id[4];
        uint64_t revision;
        uint64_t* response; //pointer to the response revision.
    };

    void CheckLimineTags()
    {
        //we require these tags as a bare minimum
        ASSERT(hhdmRequest.response != nullptr, "HHDM response required");
        ASSERT(memmapRequest.response != nullptr, "Memory map response required");
        ASSERT(kernelAddrRequest.response != nullptr, "Kernel address required");

        const bool printDetails = Config::GetConfigNumber("kernel.boot.print_tags", true);
        const size_t requestCount = (sizeof(requests) / sizeof(void*)) - 1;
        
        size_t responsesFound = 0;
        for (size_t i = 0; i < requestCount; i++)
        {
            auto req = reinterpret_cast<const LimineReq*>(requests[i]);
            if (req->response == nullptr)
            {
                Log("%s request: rev=%" PRIu64", no response.", LogLevel::Verbose, 
                    RequestNameStrs[i], req->revision);
            }
            else
            {
                Log("%s request: rev=%" PRIu64", respRev=%" PRIu64, LogLevel::Verbose,
                    RequestNameStrs[i], req->revision, *req->response);
                if (printDetails && RequestPrinters[i] != nullptr)
                    RequestPrinters[i]();
                responsesFound++;
            }
        }

        Log("Bootloader populated %zu/%zu responses.", LogLevel::Verbose, 
            responsesFound, requestCount);
    }
}
