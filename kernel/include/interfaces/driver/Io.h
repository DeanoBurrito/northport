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

typedef enum
{
    npk_iop_type_read = 0,
    npk_iop_type_write = 1,
} npk_iop_type;

typedef struct
{
    npk_iop_type op_type;
} npk_iop_context;

typedef struct
{
    void* buffer;
    uintptr_t addr;
    size_t length;
    npk_handle descriptor_id;
    void* descriptor_data;
} npk_iop_frame;

typedef struct
{
    npk_handle device_api_id;
    npk_iop_type type;
    void* buffer;
    size_t length;
    uintptr_t addr;
} npk_iop_beginning;

npk_handle npk_begin_iop(REQUIRED npk_iop_beginning* begin);
bool npk_end_iop(npk_handle iop);

#ifdef __cplusplus
}
#endif
