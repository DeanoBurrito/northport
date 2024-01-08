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
#include "Api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    npk_load_type type;
    size_t length;
    REQUIRED OWNING const uint8_t* str;
} npk_load_name;

typedef struct
{
    size_t load_name_count;
    REQUIRED OWNING npk_load_name* load_names;
    OPTIONAL OWNING npk_init_tag* init_data;
    OPTIONAL OWNING npk_string friendly_name;
    OPTIONAL void* driver_data;
} npk_device_desc;

npk_handle npk_add_device_desc(REQUIRED OWNING npk_device_desc* descriptor, bool as_child);
bool npk_remove_device_desc(npk_handle which, OPTIONAL void** driver_data);

#ifdef __cplusplus
}
#endif
