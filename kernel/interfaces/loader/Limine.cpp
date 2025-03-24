#include <interfaces/loader/Generic.h>
#include <interfaces/loader/Limine.h>
#include <core/Log.h>

namespace Npk::Loader
{
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
    limine_rsdp_request rsdpRequest
    {
        .id = LIMINE_RSDP_REQUEST,
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
    limine_kernel_address_request kernelAddrRequest
    {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    void GetData(LoaderData& data)
    {
        ASSERT_(hhdmRequest.response != nullptr);
        ASSERT_(memmapRequest.response != nullptr);
        ASSERT_(kernelAddrRequest.response != nullptr);

        data.directMapBase = hhdmRequest.response->offset;
        data.kernelPhysBase = kernelAddrRequest.response->physical_base;
        if (rsdpRequest.response != nullptr && rsdpRequest.response->address != nullptr)
        {
            data.rsdp = reinterpret_cast<Paddr>(rsdpRequest.response->address);
            if (*data.rsdp > data.directMapBase)
                *data.rsdp -= data.directMapBase;
        }
        if (dtbRequest.response != nullptr && dtbRequest.response->dtb_ptr != nullptr)
        {
            data.fdt = reinterpret_cast<Paddr>(dtbRequest.response->dtb_ptr);
            if (*data.fdt > data.directMapBase)
                *data.fdt -= data.directMapBase;
        }

        for (size_t i = memmapRequest.response->entry_count; i != 0; i--)
        {
            const auto entry = memmapRequest.response->entries[i - 1];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;

            data.directMapLength = entry->base + entry->length;
            break;
        }

        if (framebufferRequest.response != nullptr 
            && framebufferRequest.response->framebuffer_count > 0)
        {
            /* The limine boot protocol (and bootloader) do support multiple framebuffers,
             * however most firmware doesn't expose multiple framebuffers.
             * It's far easier to only single bootloader provided framebuffer, so
             * thats what we'll do for now.
             */
            const auto loaderFb = framebufferRequest.response->framebuffers[0];

            data.framebuffer.address = reinterpret_cast<Paddr>(loaderFb->address);
            if (data.framebuffer.address > data.directMapBase)
                data.framebuffer.address -= data.directMapBase;
            data.framebuffer.width = static_cast<uint32_t>(loaderFb->width);
            data.framebuffer.height = static_cast<uint32_t>(loaderFb->height);
            data.framebuffer.stride = loaderFb->pitch;

            data.framebuffer.redShift = loaderFb->red_mask_shift;
            data.framebuffer.greenShift = loaderFb->green_mask_shift;
            data.framebuffer.blueShift = loaderFb->blue_mask_shift;
            data.framebuffer.redBits = loaderFb->red_mask_size;
            data.framebuffer.greenBits = loaderFb->green_mask_size;
            data.framebuffer.blueShift = loaderFb->blue_mask_size;
            data.framebuffer.valid = true;
        }
        else
            data.framebuffer.valid = false;
    }

    sl::StringSpan GetCommandLine()
    {
        return {}; //TODO: populate
    }

    size_t GetMemmapUsable(sl::Span<MemmapEntry> store, size_t offset)
    {
        size_t entryHead = 0;
        
        for (size_t i = 0; i < memmapRequest.response->entry_count; i++)
        {
            const auto entry = memmapRequest.response->entries[i];
            if (entry->type != LIMINE_MEMMAP_USABLE)
                continue;

            if (offset > 0)
            {
                offset--;
                continue;
            }

            if (entryHead == store.Size())
                break;

            store[entryHead].base = static_cast<Paddr>(entry->base);
            store[entryHead].length = static_cast<size_t>(entry->length);
            entryHead++;
        }

        return entryHead;
    }
}
