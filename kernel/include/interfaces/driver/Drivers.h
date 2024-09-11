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
#include "Filesystem.h"
#include "Io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    npk_device_api_type_io = 0,
    npk_device_api_type_framebuffer = 1,
    npk_device_api_type_gpu = 2,
    npk_device_api_type_filesystem = 3,
    npk_device_api_type_syspower = 4,
} npk_device_api_type;

struct npk_device_api_
{
    size_t id;
    npk_device_api_type type;
    OPTIONAL void* driver_data;

    OPTIONAL npk_string (*get_summary)(npk_device_api_* api);
};

typedef struct npk_device_api_ npk_device_api;

typedef struct
{
    npk_device_api header;

    REQUIRED bool (*begin_op)(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame);
    REQUIRED bool (*end_op)(npk_device_api* api, npk_iop_context* context, npk_iop_frame* iop_frame);
} npk_io_device_api;

typedef struct
{
    uint8_t shift_r;
    uint8_t shift_g;
    uint8_t shift_b;
    uint8_t shift_a;
    uint8_t mask_r;
    uint8_t mask_g;
    uint8_t mask_b;
    uint8_t mask_a;
} npk_pixel_format;

typedef struct
{
    size_t width;
    size_t height;
    size_t bpp;
    size_t stride;
    npk_pixel_format format;
} npk_framebuffer_mode;

typedef struct
{
    npk_device_api header;
    REQUIRED npk_framebuffer_mode (*get_mode)(npk_device_api* api);
    OPTIONAL bool (*set_mode)(npk_device_api* api, REQUIRED const npk_framebuffer_mode* mode);
    OPTIONAL void (*begin_draw)(npk_device_api* api);
    OPTIONAL void (*end_draw)(npk_device_api* api, size_t x, size_t y, size_t w, size_t h);
} npk_framebuffer_device_api;

typedef struct
{
    npk_handle id;
    size_t edid_size;
} npk_scanout_info;

typedef struct
{
    npk_device_api header;

    REQUIRED npk_handle (*create_framebuffer)(npk_device_api* api, size_t x, size_t y, npk_pixel_format format);
    REQUIRED bool (*destroy_framebuffer)(npk_device_api* api, npk_handle fb);
    REQUIRED bool (*set_scanout_framebuffer)(npk_device_api* api, npk_handle scanout_index, npk_framebuffer_device_api* fb);
    REQUIRED size_t (*get_scanout_info)(npk_device_api* api, REQUIRED npk_scanout_info* buff, size_t buff_count, size_t first);
} npk_gpu_device_api;

typedef struct
{
    npk_device_api* api;
    npk_handle node_id;
    void* node_data;
} npk_fs_context;

typedef enum
{
    npk_fs_attrib_flag_size = 1 << 0,
    npk_fs_attrib_flag_name = 1 << 1,
    npk_fs_attrib_flag_caps = 1 << 2,
} npk_fs_attrib_flags;

typedef struct
{
    npk_device_api header;

    REQUIRED npk_fsnode_type (*enter_cache)(npk_device_api* api, npk_handle id, void** driver_data);
    REQUIRED bool (*exit_cache)(npk_device_api* api, npk_handle id, void* data);
    REQUIRED npk_handle (*get_root)(npk_device_api* api);
    REQUIRED bool (*mount)(npk_device_api* api);
    REQUIRED bool (*unmount)(npk_device_api* api);

    REQUIRED npk_handle (*create)(npk_fs_context context, npk_fsnode_type type, npk_string name);
    REQUIRED bool (*remove)(npk_fs_context context, npk_handle dir);
    REQUIRED npk_handle (*find_child)(npk_fs_context context, npk_string name);
    REQUIRED bool (*get_attribs)(npk_fs_context context, npk_fs_attribs* attribs);
    REQUIRED bool (*set_attribs)(npk_fs_context context, const npk_fs_attribs* attribs, npk_fs_attrib_flags flags);
    REQUIRED bool (*read_dir)(npk_fs_context context, size_t* count, npk_dir_entry** listing);
} npk_filesystem_device_api;

typedef struct
{
    npk_device_api header;

    OPTIONAL bool (*power_off)(npk_device_api* api);
    OPTIONAL bool (*reboot)(npk_device_api* api);
} npk_sys_power_device_api;

bool npk_add_device_api(REQUIRED npk_device_api* api);
bool npk_remove_device_api(size_t device_id);
bool npk_set_transport_api(npk_handle api_id);

#ifdef __cplusplus
}
#endif
