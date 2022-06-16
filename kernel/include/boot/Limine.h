#pragma once

#include <boot/LimineProtocol.h>

namespace Kernel::Boot
{
    extern limine_bootloader_info_request bootloaderInfoRequest;
    extern limine_stack_size_request stackSizeRequest;
    extern limine_hhdm_request hhdmRequest;
    extern limine_framebuffer_request framebufferRequest;
    extern limine_5_level_paging_request la57PagingRequest;
    extern limine_memmap_request memmapRequest;
    extern limine_kernel_file_request kernelFileRequest;
    extern limine_module_request modulesRequest;
    extern limine_rsdp_request rsdpRequest;
    extern limine_efi_system_table_request efiTableRequest;
    extern limine_boot_time_request bootTimeRequest;
    extern limine_kernel_address_request kernelAddrRequest;

    void PrintBootInfo();
}
