#include <boot/LimineTags.h>
#include <debug/Log.h>

namespace Npk::Boot
{
    limine_bootloader_info_request bootloaderInfoRequest
    {
        .id = LIMINE_BOOTLOADER_INFO_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_stack_size_request stackSizeRequest
    {
        .id = LIMINE_STACK_SIZE_REQUEST,
        .revision = 0,
        .response = nullptr,
        .stack_size = 0x2000
    };

    limine_hhdm_request hhdmRequest
    {
        .id = LIMINE_HHDM_REQUEST,
        .revision = 0,
        .response = nullptr
    };
    
    limine_framebuffer_request framebufferRequest
    {
        .id = LIMINE_FRAMEBUFFER_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_paging_mode_request pagingModeRequest
    {
        .id = LIMINE_PAGING_MODE_REQUEST,
        .revision = 0,
        .response = nullptr,
        .mode = LIMINE_PAGING_MODE_MAX,
        .flags = 0,
    };

    limine_memmap_request memmapRequest
    {
        .id = LIMINE_MEMMAP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_module_request modulesRequest
    {
        .id = LIMINE_MODULE_REQUEST,
        .revision = 1,
        .response = nullptr,
        .internal_module_count = 0,
        .internal_modules = nullptr
    };

    limine_rsdp_request rsdpRequest
    {
        .id = LIMINE_RSDP_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_efi_system_table_request efiTableRequest
    {
        .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_boot_time_request bootTimeRequest
    {
        .id = LIMINE_BOOT_TIME_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_kernel_address_request kernelAddrRequest
    {
        .id = LIMINE_KERNEL_ADDRESS_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_dtb_request dtbRequest
    {
        .id = LIMINE_DTB_REQUEST,
        .revision = 0,
        .response = nullptr
    };

    limine_smp_request smpRequest
    {
        .id = LIMINE_SMP_REQUEST,
        .revision = 0,
        .response = nullptr,
        .flags = 0
    };

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

        const size_t requestCount = (sizeof(requests) / sizeof(void*)) - 1;
        
        size_t responsesFound = 0;
        for (size_t i = 0; i < requestCount; i++)
        {
            auto req = reinterpret_cast<const LimineReq*>(requests[i]);
            if (req->response == nullptr)
                Log("%s request: no response.", LogLevel::Verbose, RequestNameStrs[i]);
            else
            {
                Log("%s request: revision=%lu, responseRevision=%lu", LogLevel::Verbose,
                    RequestNameStrs[i], req->revision, *req->response);
                responsesFound++;
            }
        }

        Log("Bootloader populated %lu (out of %lu) responses.", LogLevel::Verbose, 
            responsesFound, requestCount);
    }
}
