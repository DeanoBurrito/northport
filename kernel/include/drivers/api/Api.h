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

#ifdef __cplusplus
extern "C" {
#endif

/* API version defined by this header */
#define NP_MODULE_API_VER_MAJOR 0
#define NP_MODULE_API_VER_MINOR 1
#define NP_MODULE_API_VER_REV 0

/* Various GUIDs used by the API */
#define NP_MODULE_META_START_GUID { 0x11, 0xfc, 0x92, 0x87, 0x64, 0xc0, 0x4b, 0xaf, 0x9e, 0x59, 0x31, 0x64, 0xbf, 0xf9, 0xfa, 0x5a }
#define NP_MODULE_MANIFEST_GUID { 0x23, 0x1e, 0x1f, 0xeb, 0xcf, 0xf6, 0x4f, 0xfc, 0x97, 0x76, 0x26, 0x07, 0x42, 0x77, 0x18, 0x96 }

/* Struct that indicates this file can be loaded as a module, only one of these can exist. */
typedef struct
{
    uint8_t metadata_start_guid[16];
    uint16_t api_ver_major;
    uint16_t api_ver_minor;
    uint16_t api_ver_rev;
} npk_module_metadata;

typedef enum
{
    Never = 0,
    Always = 1,
    PciClass = 2,
    PciId = 3,
    DtbCompat = 4,
} npk_load_type;

typedef struct 
{
    uint8_t manifest_start_guid[16];
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_rev;

    npk_load_type load_type;
    const uint8_t* load_str;
    const char* friendly_name;

    void (*entry)();
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
void* npk_heap_alloc(size_t count);
void npk_heap_free(void* ptr, size_t count);

void npk_log(const char* str, npk_log_level level, ...);
void npk_panic(const char* why);

void npk_thread_exit(size_t code);

#ifdef __cplusplus
}
#endif
