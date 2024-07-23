// spdx-license-identifier: 0BSD

/* Copyright (C) 2023-24 Bryce Lanham, et al.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _CROW_H
#define _CROW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef union crow_fourcc {
	uint32_t value;
	char string[4];
} crow_fourcc_t;

#define CROW_FOURCC(str) ((crow_fourcc_t){\
    .value = str[0] \
    | (uint32_t)str[1] << 8 \
    | (uint32_t)str[2] << 16 \
    | (uint32_t)str[3] << 24 \
})

// Bootloader Info
#define CROW_REQ_BLDR CROW_FOURCC("BLDR")
typedef struct crow_bootloader_info {
	uint32_t protocol_version;
	char name[32];
	uint32_t version;
} crow_bootloader_info_t;

typedef struct crow_request_bldr {
	crow_fourcc_t id; // CROW_REQ_BLDR
	crow_bootloader_info_t *response;
} crow_request_bldr_t;

// Higher Half Direct Mapping
#define CROW_REQ_HHDM CROW_FOURCC("HHDM")
typedef struct crow_hhdm_info {
	uint32_t base_address;
} crow_hhdm_info_t;

typedef struct crow_request_hhdm {
	crow_fourcc_t id; // CROW_REQ_HHDM
	crow_hhdm_info_t *response;
} crow_request_hhdm_t;

// Display Info
#define CROW_REQ_DISP CROW_FOURCC("DISP")
typedef enum crow_display_type {
	CROW_DISPLAY_TYPE_NONE,
	CROW_DISPLAY_TYPE_CGA,
	CROW_DISPLAY_TYPE_VGA,
	CROW_DISPLAY_TYPE_VBE,
	CROW_DISPLAY_TYPE_AMIGA,
	CROW_DISPLAY_TYPE_RAMFB,
	CROW_DISPLAY_TYPE_COUNT // must be last
} crow_display_type_t;

typedef struct crow_display_info {
	crow_display_type_t type;
	uint32_t framebuffer_addr;
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t pitch;
	void *edid;
	void *aux; // platform-specific data
} crow_display_info_t;

typedef struct crow_display_response {
	crow_display_info_t *displays;	
	uint32_t num_displays;
} crow_display_response_t;

typedef struct crow_request_disp {
	crow_fourcc_t id; // CROW_REQ_DISP
	crow_display_response_t *response;
} crow_request_disp_t;

// Platform Info
#define CROW_REQ_PLAT CROW_FOURCC("PLAT")
typedef struct crow_platform_info {
	uint32_t type;
	uint32_t cpu_type;
	uint32_t cpu_flags;
	char name[32];

	void *aux; // platform-specific data
} crow_platform_info_t;

typedef struct crow_request_plat {
	crow_fourcc_t id; // CROW_REQ_PLAT
	crow_platform_info_t *response;
} crow_request_plat_t;

// Page Size
#define CROW_REQ_PGSZ CROW_FOURCC("PGSZ")
typedef struct crow_page_size {
	uint32_t size;
} crow_page_size_t;

typedef struct crow_request_pgsz {
	crow_fourcc_t id; // CROW_REQ_PGSZ
	crow_page_size_t *response;
} crow_request_pgsz_t;

// Firmware Info
#define CROW_REQ_FW CROW_FOURCC("BIOS") // (couldn't help myself)
typedef struct crow_firmware_info {
	char vendor[32];
	char version[32];
	char name[32];

	void *aux; // platform-specific data
} crow_firmware_info_t;

typedef struct crow_request_fw {
	crow_fourcc_t id; // CROW_REQ_FW
	crow_firmware_info_t *response;
} crow_request_fw_t;

// Memory Map
#define CROW_REQ_MMAP CROW_FOURCC("MMAP")
typedef enum crow_memmap_type {
	CROW_MEMMAP_TYPE_BAD_MEMORY,
	CROW_MEMMAP_TYPE_USABLE,
	CROW_MEMMAP_TYPE_RESERVED,
	CROW_MEMMAP_TYPE_KERNEL,
	CROW_MEMMAP_TYPE_KERNEL_SYMBOLS,
	CROW_MEMMAP_TYPE_MODULE,
	CROW_MEMMAP_TYPE_RAMDISK,
	CROW_MEMMAP_TYPE_INITRD = CROW_MEMMAP_TYPE_RAMDISK,
	CROW_MEMMAP_TYPE_FRAMEBUFFER,
	CROW_MEMMAP_TYPE_BOOTLOADER_RECLAIMABLE,
	CROW_MEMMAP_TYPE_BOOTLOADER_RESERVED,
	CROW_MEMMAP_TYPE_ACPI_RECLAIMABLE,
	CROW_MEMMAP_TYPE_ACPI_NVS,
	CROW_MEMMAP_TYPE_EFI,
	CROW_MEMMAP_TYPE_COUNT // must be last
} crow_memmap_t;

typedef struct crow_memmap_entry {
	uint32_t start_addr;
	uint32_t size;
	crow_memmap_t type;
	uint32_t attributes;
	char name[32];
} crow_memmap_entry_t;

typedef struct crow_memory_map {
	uint32_t num_entries;
	crow_memmap_entry_t entries[];
} crow_memory_map_t;

typedef struct crow_request_mmap {
	crow_fourcc_t id; // CROW_REQ_MMAP
	crow_memory_map_t *response;
} crow_request_mmap_t;

// Symbol Table
#define CROW_REQ_SYMT CROW_FOURCC("STAB")
typedef struct crow_symbol_table {
  	uint32_t num_symbols;
  	uint32_t symbols_addr;
  	uint32_t strings_addr;
} crow_symbol_table_t;

typedef struct crow_request_symt {
	crow_fourcc_t id; // CROW_REQ_SYMT
	crow_symbol_table_t *response;
} crow_request_symt_t;

// Boot Time
#define CROW_REQ_TIME CROW_FOURCC("TIME")
typedef struct crow_boot_time {
	uint32_t unix_timestamp;
} crow_boot_time_t;

typedef struct crow_request_time {
	crow_fourcc_t id; // CROW_REQ_TIME
	crow_boot_time_t *response;
} crow_request_time_t;

// Boot Media
#define CROW_REQ_BMED CROW_FOURCC("BMED")
typedef enum crow_boot_media_type {
	CROW_BOOT_MEDIA_TYPE_NONE,
	CROW_BOOT_MEDIA_TYPE_DISK,
	CROW_BOOT_MEDIA_TYPE_CDROM,
	CROW_BOOT_MEDIA_TYPE_USB,
	CROW_BOOT_MEDIA_TYPE_NETWORK,
	CROW_BOOT_MEDIA_TYPE_COUNT // must be last
} crow_boot_media_type_t;

typedef struct crow_boot_media {
	crow_boot_media_type_t type;
	char path[64];
	uint32_t ip;
} crow_boot_media_t;

typedef struct crow_request_mdia {
	crow_fourcc_t id; // CROW_REQ_BMED
	crow_boot_media_t *response;
} crow_request_mdia_t;

// Bootstrap TTY
#define CROW_REQ_BTTY CROW_FOURCC("BTTY")
typedef void (*btty_putchar_t)(int c);

typedef struct crow_bootstrap_tty {
    btty_putchar_t putchar;
} crow_bootstrap_tty_t;

typedef struct crow_request_btty {
    crow_fourcc_t id;  // CROW_REQ_BTTY
    crow_bootstrap_tty_t* response;
} crow_request_btty_t;

// Generic request structure (for iteration purposes)
typedef struct crow_request {
	crow_fourcc_t id;
	void *response;
} crow_request_t;

typedef struct crow_boot_info {
	crow_request_t *requests;
	uint32_t num_requests;
	void (*kernel_entry)(void);
} crow_boot_info_t;

typedef crow_boot_info_t (*crow_entry_point_t)(void);

// Function prototype
crow_boot_info_t crow_entry_point(void);

#ifdef __cplusplus
}
#endif

#endif
