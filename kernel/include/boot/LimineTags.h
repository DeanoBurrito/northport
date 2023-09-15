#pragma once

#include <boot/Limine.h>

namespace Npk::Boot
{
    extern limine_bootloader_info_request bootloaderInfoRequest;
    extern limine_hhdm_request hhdmRequest;
    extern limine_framebuffer_request framebufferRequest;
    extern limine_memmap_request memmapRequest;
    extern limine_module_request modulesRequest;
    extern limine_rsdp_request rsdpRequest;
    extern limine_efi_system_table_request efiTableRequest;
    extern limine_boot_time_request bootTimeRequest;
    extern limine_kernel_address_request kernelAddrRequest;
    extern limine_dtb_request dtbRequest;
    extern limine_smp_request smpRequest;
    extern limine_kernel_file_request kernelFileRequest;

    void CheckLimineTags();
}
