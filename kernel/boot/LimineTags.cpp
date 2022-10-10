#include <boot/LimineTags.h>

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

    limine_5_level_paging_request la57PagingRequest
    {
        .id = LIMINE_5_LEVEL_PAGING_REQUEST,
        .revision = 0,
        .response = nullptr
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
        .revision = 0,
        .response = nullptr
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

    [[gnu::used, gnu::section(".limine_reqs")]]
    void* requests[] = 
    {
        &bootloaderInfoRequest,
        &stackSizeRequest,
        &hhdmRequest,
        &framebufferRequest,
        &la57PagingRequest,
        &memmapRequest,
        &modulesRequest,
        &rsdpRequest,
        &efiTableRequest,
        &bootTimeRequest,
        &kernelAddrRequest,
        &dtbRequest,
        &smpRequest,
        nullptr
    };
}
