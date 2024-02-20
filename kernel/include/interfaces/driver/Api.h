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
#include "Decorators.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NPK_METADATA __attribute__((used, section(".npk_module")))

/* API version defined by this header */
#define NP_MODULE_API_VER_MAJOR 0
#define NP_MODULE_API_VER_MINOR 2
#define NP_MODULE_API_VER_REV 3

/* Various GUIDs used by the API */
#define NP_MODULE_META_START_GUID { 0x11, 0xfc, 0x92, 0x87, 0x64, 0xc0, 0x4b, 0xaf, 0x9e, 0x59, 0x31, 0x64, 0xbf, 0xf9, 0xfa, 0x5a }
#define NP_MODULE_MANIFEST_GUID { 0x23, 0x1e, 0x1f, 0xeb, 0xcf, 0xf6, 0x4f, 0xfc, 0x97, 0x76, 0x26, 0x07, 0x42, 0x77, 0x18, 0x96 }

/* Struct that indicates this file can be loaded as a module, only one of these can exist. */
typedef struct
{
    uint8_t guid[16];
    uint16_t api_ver_major;
    uint16_t api_ver_minor;
    uint16_t api_ver_rev;
} npk_module_metadata;

typedef enum
{
    PciFunction = 0,
    DtbPath = 1,
    PciHostAdaptor = 2,
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
    Ecam = 0,
    X86PortIo = 1,
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

typedef enum
{
    Never = 0,
    Always = 1,
    PciClass = 2,
    PciId = 3,
    PciHost = 4,
    DtbCompat = 5,
} npk_load_type;

#define NPK_PCI_ID_LOAD_STR(vendor, device) { ((vendor) >> 16) & 0xFF, (vendor) & 0xFF, ((device) >> 16) & 0xFF, (device) & 0xFF }
#define NPK_PCI_CLASS_LOAD_STR(cl, subcl, iface) { cl, subcl, iface }

typedef enum
{
    Init = 0,
    Exit = 1,
    AddDevice = 2,
    RemoveDevice = 3,
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
    uint8_t guid[16];
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_rev;

    npk_load_type load_type;
    size_t load_str_len;
    REQUIRED const uint8_t* load_str;
    REQUIRED const char* friendly_name;

    REQUIRED bool (*process_event)(npk_event_type type, void* arg);
} npk_driver_manifest;

typedef enum
{
    Fatal = 0,
    Error = 1,
    Warning = 2,
    Info = 3,
    Verbose = 4,
    Debug = 5,
} npk_log_level;

/* Global functions available to any drivers within a kernel module */
void npk_log(REQUIRED const char* str, npk_log_level level);
void npk_panic(REQUIRED const char* why);

#ifdef __cplusplus
}
#endif
