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

#include <stddef.h>
#include <stdint.h>
#include "__Bindings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Member variables marked as NPK_OWNING indicate that the struct is responsible for
 * managing the memory and resources associated with the marked pointer.
 * Whoever destroys/frees the struct is also
 */
#define NPK_OWNING
#define NPK_REQUIRED
#define NPK_OPTIONAL

/* A number of NPK API structs are only for use by the kernel (they may use semantics that dont
 * translate across the api boundary), however drivers may find some use in
 * managing the memory for these structures. As a compromise, placeholder structs
 * are defined throughout the API headers with the correct size and alignment.
 * The magic size and alignment values are pulled from "__Bindings.h",
 * and the following macro (`NPK_STRUCT_OPAQUE`) is to make my life a bit easier.
 *
 * If you see something like `NPK_STRUCT_OPAQUE(a_thing)`, that means the struct
 * is opaque to the driver, but its memory can still be managed. This example
 * struct would also have the `npk_a_thing`.
 */
#define NPK_STRUCT_OPAQUE(name) \
    typedef struct \
    { \
        _Alignas(NPK_ALIGNOF_STRUCT_##name) char data[NPK_SIZEOF_STRUCT_##name]; \
    } npk_##name;

typedef void* npk_handle;
typedef size_t npk_cpu_id;
typedef uintptr_t npk_paddr;
typedef uintptr_t npk_io_addr;
typedef size_t npk_uint;
typedef bool npk_bool;

typedef struct
{
    npk_uint length;
    const char* text;
} npk_string;

typedef struct
{
    npk_uint length;
    char* data;
} npk_buffer_rw;

typedef struct
{
    npk_uint length;
    const char* data;
} npk_buffer_ro;

typedef enum
{
    npk_status_ok = 0,
    npk_status_aborted = 1,
    npk_status_cancelled = 2,
} npk_status;

#ifdef __cplusplus
}
#endif
