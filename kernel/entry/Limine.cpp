#include <EntryPrivate.hpp>
#include <Core.hpp>
#include <Memory.hpp>
#include "Limine.h"

namespace Npk::Loader
{
    limine_hhdm_request hhdmReq
    {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_memmap_request mmapReq
    {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_kernel_address_request kernAddrReq
    {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_rsdp_request rsdpReq
    {
        .id = LIMINE_RSDP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_dtb_request fdtReq
    {
        .id = LIMINE_DTB_REQUEST,
        .revision = 0,
        .response = nullptr
    };
    
    limine_kernel_file_request fileReq
    {
        .id = LIMINE_KERNEL_FILE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_boot_time_request timeReq
    {
        .id = LIMINE_BOOT_TIME_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_efi_system_table_request efiReq
    {
        .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_framebuffer_request fbReq
    {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    LoadState GetEntryState()
    {
        NPK_ASSERT(hhdmReq.response != nullptr);
        NPK_ASSERT(mmapReq.response != nullptr);
        NPK_ASSERT(kernAddrReq.response != nullptr);
        
        const uintptr_t hhdm = hhdmReq.response->offset;

        sl::Opt<Paddr> rsdp {};
        if (rsdpReq.response != nullptr)
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(rsdpReq.response->address);
            if (addr >= hhdm)
                addr -= hhdm;

            if (addr != 0)
                rsdp = static_cast<Paddr>(addr);
        }

        sl::Opt<Paddr> fdt {};
        if (fdtReq.response != nullptr)
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(fdtReq.response->dtb_ptr);
            if (addr >= hhdm)
                addr -= hhdm;

            if (addr != 0)
                fdt = static_cast<Paddr>(addr);
        }

        sl::Opt<Paddr> systemTable {};
        if (efiReq.response != nullptr)
        {
            uintptr_t addr = reinterpret_cast<uintptr_t>(efiReq.response->address);
            if (addr >= hhdm)
                addr -= hhdm;

            if (addr != 0)
                systemTable = static_cast<Paddr>(addr);
        }

        sl::Opt<sl::TimePoint> timeOffset {};
        if (timeReq.response != nullptr)
            timeOffset = sl::TimePoint(timeReq.response->boot_time * sl::TimePoint::Frequency);

        sl::StringSpan cmdline {};
        if (fileReq.response != nullptr)
        {
            const size_t len = sl::MemFind(fileReq.response->kernel_file->cmdline, 0, sl::NoLimit);
            cmdline = sl::StringSpan(fileReq.response->kernel_file->cmdline, len);
        }

        return 
        {
            .directMapBase = hhdmReq.response->offset,
            .kernelBase = kernAddrReq.response->physical_base,
            .bspId = 0,
            .rsdp = rsdp,
            .fdt = fdt,
            .efiTable = systemTable,
            .timeOffset = timeOffset,
            .commandLine = cmdline
        };
    }

    size_t GetUsableRanges(sl::Span<MemoryRange> ranges, size_t offset)
    {
        size_t entryHead = 0;

        for (size_t i = 0; i < mmapReq.response->entry_count; i++)
        {
            const auto entry = mmapReq.response->entries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;

            if (offset > 0)
            {
                offset--;
                continue;
            }

            if (entryHead == ranges.Size())
                break;

            auto& store = ranges[entryHead++];
            store.base = static_cast<Paddr>(entry->base);
            store.length = static_cast<size_t>(entry->length);
        }

        return entryHead;
    }

    size_t GetFramebuffers(sl::Span<Framebuffer> fbs, size_t offset)
    {
        if (fbReq.response == nullptr)
            return 0;

        const uintptr_t dmBase = hhdmReq.response->offset;
        size_t entryHead = 0;

        for (size_t i = 0; i < fbReq.response->framebuffer_count; i++)
        {
            const auto entry = fbReq.response->framebuffers[i];
            if (entry->memory_model != LIMINE_FRAMEBUFFER_RGB)
                continue;

            if (offset > 0)
            {
                offset--;
                continue;
            }

            if (entryHead == fbs.Size())
                break;

            auto& store = fbs[entryHead++];
            store.base = (uintptr_t)entry->address;
            if (store.base >= dmBase)
                store.base -= dmBase;
            store.width = static_cast<size_t>(entry->width);
            store.height = static_cast<size_t>(entry->height);
            store.pitch = static_cast<size_t>(entry->pitch);
            store.bpp = static_cast<size_t>(entry->bpp);
            store.rShift = entry->red_mask_shift;
            store.rBits = entry->red_mask_size;
            store.gShift = entry->green_mask_shift;
            store.gBits = entry->green_mask_size;
            store.bShift = entry->blue_mask_shift;
            store.bBits = entry->blue_mask_size;
        }

        return entryHead;
    }
}
