#pragma once
/* This file defines the Northport kernel driver API. It's subject to the same license as the rest
 * of the kernel, which is included below for convinience.
 *
 * ---- LICENSE ----
 * MIT License
 *
 * Copyright (c) Dean T.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stddef.h>
#include "Primitives.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NPK_METADATA __attribute__((used, section(".npkmodule")))

/* API version defined by this header */
#define NP_MODULE_API_VER_MAJOR 0
#define NP_MODULE_API_VER_MINOR 7
#define NP_MODULE_API_VER_REV 0

#define NP_MODULE_MANIFEST_GUID { 0x23, 0x1e, 0x1f, 0xeb, 0xcf, 0xf6, 0x4f, 0xfc, 0x97, 0x76, 0x26, 0x07, 0x42, 0x77, 0x18, 0x96 }

typedef enum
{
    npk_init_tag_type_pci_function = 0,
    npk_init_tag_type_dtb_path = 1,
    npk_init_tag_type_pci_host = 2,
    npk_init_tag_type_rsdp = 3,
} npk_init_tag_type;

struct npk_init_tag_
{
    npk_init_tag_type type;
    const struct npk_init_tag_* next;
};
typedef struct npk_init_tag_ npk_init_tag;

typedef struct
{
    npk_init_tag header;

    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
} npk_init_tag_pci_function;

typedef struct
{
    npk_init_tag header;

    npk_string node_path;
} npk_init_tag_dtb_path;

typedef enum
{
    npk_pci_host_type_ecam = 0,
    npk_pci_host_type_port_io = 1,
} npk_pci_host_type;

typedef struct
{
    npk_init_tag header;

    npk_pci_host_type type;
    uintptr_t base_addr;
    uint16_t id;
    uint8_t first_bus;
    uint8_t last_bus;
} npk_init_tag_pci_host;

typedef struct
{
    npk_init_tag header;

    uintptr_t rsdp;
} npk_init_tag_rsdp;

typedef enum
{
    npk_load_type_never = 0,
    npk_load_type_always = 1,
    npk_load_type_pci_class = 2,
    npk_load_type_pci_id = 3,
    npk_load_type_pci_host = 4,
    npk_load_type_dtb_compat = 5,
    npk_load_type_acpi_runtime = 6,
    npk_load_type_acpi_pnp = 7,
} npk_load_type;

#define NPK_PCI_ID_LOAD_STR(vendor, device) { (vendor) & 0xFF, ((vendor) >> 8) & 0xFF, (device) & 0xFF, ((device) >> 8) & 0xFF }
#define NPK_PCI_CLASS_LOAD_STR(cl, subcl, iface) { cl, subcl, iface }
#define NPK_PNP_ID_STR(data) { .type = npk_load_type_acpi_pnp, .length = sizeof(data) - 1, .str = (const uint8_t*)data }

typedef enum
{
    npk_event_type_init = 0,
    npk_event_type_exit = 1,
    npk_event_type_add_device = 2,
    npk_event_type_remove_device = 3,
} npk_event_type;

typedef struct
{
    const npk_init_tag* tags;
    npk_handle descriptor_id;
} npk_event_add_device;

typedef struct
{
    size_t device_id;
} npk_event_remove_device;

typedef struct
{
    npk_load_type type;
    size_t length;
    REQUIRED OWNING const uint8_t* str;
} npk_load_name;

#define NP_MANIFEST_FLAG_NONE 0
#define NP_MANIFEST_FLAG_ALWAYS_LOAD (1 << 0)

typedef struct 
{
    uint8_t guid[16];
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t api_ver_major;
    uint16_t api_ver_minor;
    size_t flags;

    REQUIRED bool (*process_event)(npk_event_type type, void* arg);
    size_t friendly_name_len;
    REQUIRED const char* friendly_name;
    size_t load_name_count;
    REQUIRED const npk_load_name* load_names;

} npk_driver_manifest;

typedef enum
{
    npk_log_level_fatal = 0,
    npk_log_level_error = 1,
    npk_log_level_warning = 2,
    npk_log_level_info = 3,
    npk_log_level_verbose = 4,
    npk_log_level_debug = 5,
} npk_log_level;

typedef enum
{
    npk_bus_type_port_io = 0,
    npk_bus_type_pci = 1,
} npk_bus_type;

#define NPK_MAKE_PCI_BUS_ADDR(seg, bus, dev, func, reg) (((uintptr_t)(seg) << 32) | ((uintptr_t)(bus) << 20) | ((uintptr_t)(dev) << 15) | ((uintptr_t)(func) << 12) | (reg))

/* Global functions available to any drivers within a kernel module */
void npk_log(npk_string str, npk_log_level level);
bool npk_add_bus_access(npk_bus_type type, bool (*func)(size_t width, uintptr_t addr, uintptr_t* data, bool write));
bool npk_access_bus(npk_bus_type type, size_t width, uintptr_t addr, REQUIRED uintptr_t* data, bool write);

#ifdef __cplusplus
}
#endif
