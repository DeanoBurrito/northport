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

typedef enum
{
    Io = 0,
    Framebuffer = 1,
    Gpu = 2,
    Hid = 3,
} npk_device_api_type;

typedef struct
{
    size_t id;
    npk_device_api_type type;
    void* driver_data;
} npk_device_api;

typedef struct
{
} npk_iop;

typedef struct
{
    npk_device_api header;

    bool (*begin_op)(npk_iop* iop, size_t index);
    bool (*end_op)(npk_iop* iop, size_t index);
} npk_io_device_api;

typedef struct
{
    npk_device_api header;
} npk_framebuffer_device_api;

typedef struct
{
    npk_device_api header;
} npk_gpu_device_api;

typedef struct
{
    npk_device_api header;
} npk_hid_device_api;

bool npk_add_device_api(npk_device_api* api);
bool npk_remove_device_api(size_t device_id);

#ifdef __cplusplus
}
#endif
