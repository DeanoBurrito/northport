#include <interfaces/loader/Generic.h>
#include <interfaces/loader/Limine.h>
#include <Entry.h>
#include <arch/Misc.h>
#include <core/Log.h>
#include <core/Config.h>
#include <Hhdm.h>
#include <formats/Elf.h>
#include <UnitConverter.h>
#include <NativePtr.h>

#if defined(__x86_64__)
    #define LBP_CPU_ID(InfoPtr) (InfoPtr)->lapic_id
    #define LBP_CPU_IS_BSP(InfoPtr) (InfoPtr)->lapic_id == smpRequest.response->bsp_lapic_id
#elif __riscv_xlen == 64
    #define LBP_CPU_ID(InfoPtr) (InfoPtr)->hartid
    #define LBP_CPU_IS_BSP(InfoPtr) (InfoPtr)->hartid == smpRequest.response->bsp_hartid
#elif defined(__m68k__)
    #define LBP_CPU_ID(InfoPtr) (InfoPtr)->id
    #define LBP_CPU_IS_BSP(InfoPtr) (InfoPtr)->id == smpRequest.response->bsp_id
#endif

namespace Npk
{
    constexpr const char InitdiskMagicStr[] = "northport-initdisk";

    alignas(8) 
    limine_bootloader_info_request loaderInfoRequest
    {
        .id = LIMINE_BOOTLOADER_INFO_REQUEST,
        .revision = 0,
        .response = nullptr
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
        &loaderInfoRequest,
        &hhdmRequest,
        &framebufferRequest,
        &pagingModeRequest,
        &memmapRequest,
        &modulesRequest,
        &rsdpRequest,
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
        "HHDM",
        "Framebuffer",
        "Paging mode",
        "Memory map",
        "Modules",
        "RSDP",
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
            auto resp = loaderInfoRequest.response;
            Log("  name=%s, ver=%s", LogLevel::Verbose, resp->name, resp->version);
        },
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

            size_t entryTypeCounts[sizeof(EntryTypeStrs) / sizeof(const char*)];
            for (size_t i = 0; i < sizeof(entryTypeCounts) / sizeof(size_t); i++)
                entryTypeCounts[i] = 0;

            auto resp = memmapRequest.response;
            for (size_t i = 0; i < resp->entry_count; i++)
            {
                auto entry = resp->entries[i];
                entryTypeCounts[entry->type] += entry->length;
                auto conv = sl::ConvertUnits(entry->length, sl::UnitBase::Binary);
                Log("  %zu: base=0x%" PRIx64", length=0x%" PRIx64" (%zu.%zu %sB), type=%s", LogLevel::Verbose, i, 
                    entry->base, entry->length, conv.major, conv.minor, conv.prefix, EntryTypeStrs[entry->type]);
            }

            for (size_t i = 0; i < sizeof(entryTypeCounts) / sizeof(size_t); i++)
            {
                auto conv = sl::ConvertUnits(entryTypeCounts[i], sl::UnitBase::Binary);
                Log("  %s: 0x%zx (%zu.%zu %sB)", LogLevel::Verbose, EntryTypeStrs[i], 
                    entryTypeCounts[i], conv.major, conv.minor, conv.prefix);
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
                const size_t localId = LBP_CPU_ID(resp->cpus[i]);
                const bool isBsp = LBP_CPU_IS_BSP(resp->cpus[i]);
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

    void ValidateLoaderData()
    {
        const bool printDetails = Core::GetConfigNumber("kernel.boot.print_tags", true);
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

    bool GetHhdmBounds(uintptr_t& base, size_t& length)
    {
        if (hhdmRequest.response == nullptr || memmapRequest.response == nullptr)
            return false;

        base = hhdmRequest.response->offset;
        length = 0;

        for (size_t i = 0; i < memmapRequest.response->entry_count; i++)
        {
            auto entry = memmapRequest.response->entries[i];
            switch (entry->type)
            {
            case LIMINE_MEMMAP_USABLE:
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            case LIMINE_MEMMAP_KERNEL_AND_MODULES:
            case LIMINE_MEMMAP_FRAMEBUFFER:
                break;
            default:
                continue;
            }

            length = AlignUpPage(entry->base + entry->length);
        }

        ASSERT_(length != 0);
        return true;
    }

    uintptr_t GetKernelPhysAddr()
    {
        if (kernelAddrRequest.response == nullptr)
            return 0;
        
        return kernelAddrRequest.response->physical_base;
    }

    sl::Opt<uintptr_t> EarlyPmAlloc(size_t length)
    {
        if (memmapRequest.response == nullptr)
            return {};

        length = AlignUpPage(length);
        for (size_t i = 0; i < memmapRequest.response->entry_count; i++)
        {
            auto entry = memmapRequest.response->entries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;
            if (entry->length < length)
                continue;

            const uintptr_t base = entry->base;
            entry->base += length;
            entry->length -= length;

            return base;
        }

        return {};
    }

    static size_t GetMemmapEntries(sl::Span<MemmapEntry> entries, size_t offset, uint64_t type)
    {
        if (memmapRequest.response == nullptr)
            return 0;

        size_t entryHead = 0;
        for (size_t i = 0; i < memmapRequest.response->entry_count; i++)
        {
            auto entry = memmapRequest.response->entries[i];
            if (entry->type != type)
                continue;
            if (offset > 0)
            {
                offset--;
                continue;
            }

            if (entryHead == entries.Size())
                break;

            MemmapEntry& store = entries[entryHead++];
            store.base = entry->base;
            store.length = entry->length;
        }

        return entryHead;
    }

    size_t GetUsableMemmap(sl::Span<MemmapEntry> entries, size_t offset)
    { return GetMemmapEntries(entries, offset, LIMINE_MEMMAP_USABLE); }

    size_t GetReclaimableMemmap(sl::Span<MemmapEntry> entries, size_t offset)
    { return GetMemmapEntries(entries, offset, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE); }

    sl::Opt<uintptr_t> GetRsdp()
    {
        if (rsdpRequest.response == nullptr || rsdpRequest.response->address == 0)
            return {};

        return reinterpret_cast<uintptr_t>(rsdpRequest.response->address) - hhdmBase;
    }

    sl::Opt<uintptr_t> GetDtb()
    {
        if (dtbRequest.response == nullptr || dtbRequest.response->dtb_ptr == 0)
            return {};

        return reinterpret_cast<uintptr_t>(dtbRequest.response->dtb_ptr) - hhdmBase;
    }

    sl::Span<uint8_t> GetInitdisk()
    {
        if (modulesRequest.response == nullptr)
            return {};

        for (size_t i = 0; i < modulesRequest.response->module_count; i++)
        {
            auto mod = modulesRequest.response->modules[i];
            if (sl::memcmp(mod->cmdline, InitdiskMagicStr, sizeof(InitdiskMagicStr)) != 0)
                continue;

            return sl::Span(reinterpret_cast<uint8_t*>(mod->address), mod->size);
        }

        return {};
    }

    sl::Span<uint8_t> GetKernelSymbolTable()
    {
        if (kernelFileRequest.response == nullptr)
            return {};
        
        sl::NativePtr file = kernelFileRequest.response->kernel_file->address;
        auto ehdr = file.As<const sl::Elf_Ehdr>();
        auto shdrs = file.Offset(ehdr->e_shoff).As<sl::Elf_Shdr>();

        for (size_t i = 0; i < ehdr->e_shnum; i++)
        {
            const auto& shdr = shdrs[i];
            if (shdr.sh_type != sl::SHT_SYMTAB)
                continue;

            return { file.Offset(shdr.sh_offset).As<uint8_t>(), shdr.sh_size };
        }

        return {};
    }

    sl::Span<const char> GetKernelStringTable()
    {
        if (kernelFileRequest.response == nullptr)
            return {};

        sl::NativePtr file = kernelFileRequest.response->kernel_file->address;
        auto ehdr = file.As<const sl::Elf_Ehdr>();
        auto shdrs = file.Offset(ehdr->e_shoff).As<sl::Elf_Shdr>();

        for (size_t i = 0; i < ehdr->e_shnum; i++)
        {
            const auto& shdr = shdrs[i];
            if (shdr.sh_type != sl::SHT_SYMTAB)
                continue;

            const auto& linked = shdrs[shdr.sh_link];
            return { file.Offset(linked.sh_offset).As<const char>(), linked.sh_size };
        }

        return {};
    }

    static void ApEntry(limine_smp_info* info)
    {
        PerCoreEntry(LBP_CPU_ID(info), false);
        ExitCoreInit();
    }

    size_t StartupAps()
    {
        if (smpRequest.response == nullptr)
        {
            PerCoreEntry(0, true);
            return 1;
        }

        const bool inhibitApStartup = Core::GetConfigNumber("kernel.smp.inhibit", false);
        for (size_t i = 0; i < smpRequest.response->cpu_count; i++)
        {
            auto cpu = smpRequest.response->cpus[i];
            if (LBP_CPU_IS_BSP(cpu))
                PerCoreEntry(LBP_CPU_ID(cpu), true);
            else if (!inhibitApStartup)
                __atomic_store_n(&cpu->goto_address, &ApEntry, __ATOMIC_RELAXED);
        }

        if (inhibitApStartup)
        {
            Log("Bootloader started %" PRIu64" cores, but 'kernel.smp.inhibit' was passed - only BSP will run kernel code.",
                LogLevel::Warning, smpRequest.response->cpu_count);
        }

        //Even if AP startup was prevented, the cores are still running bootloader code, which lives in
        //reclaimable memory - so we actually cant reclaim that without crashing those cores later.
        //I'm not sure what this would do to the system overall, so I'm going to count them as valid
        //references to bootloader data.
        return smpRequest.response->cpu_count;
    }

    sl::StringSpan GetCommandLine()
    {
        if (kernelFileRequest.response == nullptr)
            return {};

        const char* str = kernelFileRequest.response->kernel_file->cmdline;
        return { str, sl::memfirst(str, 0, 0) };
    }

    size_t GetFramebuffers(sl::Span<LoaderFramebuffer> fbs, size_t offset)
    {
        if (framebufferRequest.response == nullptr)
            return 0;

        size_t head = 0;
        for (size_t i = 0; i < framebufferRequest.response->framebuffer_count; i++)
        {
            auto fb = framebufferRequest.response->framebuffers[i];
            if (i < offset)
                continue;

            if (head == fbs.Size())
                break;

            auto& outFb = fbs[head++];
            outFb.address = reinterpret_cast<uintptr_t>(fb->address);
            outFb.width = fb->width;
            outFb.height = fb->height;
            outFb.stride = fb->pitch;

            //needed because ASSERT concats this into a format string (the % causes errors lol)
            const size_t bppMod4 = fb->bpp % 4;
            ASSERT_(bppMod4 == 0);

            outFb.pixelStride = fb->bpp / 4;
            outFb.rShift = fb->red_mask_shift;
            outFb.gShift = fb->green_mask_shift;
            outFb.bShift = fb->blue_mask_shift;
            outFb.rBits = fb->red_mask_size;
            outFb.gBits = fb->green_mask_size;
            outFb.bBits = fb->blue_mask_size;
        }

        return head;
    }
}
