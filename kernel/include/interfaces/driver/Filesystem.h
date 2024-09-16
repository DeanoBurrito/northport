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
#include "Primitives.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    npk_handle device_id;
    npk_handle node_id;
} npk_fs_id;

typedef enum
{
    npk_fsnode_type_file = 0,
    npk_fsnode_type_dir = 1,
    npk_fsnode_type_link = 2,
} npk_fsnode_type;

typedef struct
{
    size_t size;
    npk_string name;
} npk_fs_attribs;

typedef struct
{
    npk_fs_id id;
} npk_dir_entry;

typedef struct
{
    bool writable;
    bool uncachable;
} npk_mount_options;

npk_fs_id npk_fs_lookup(npk_string path);
npk_string npk_fs_get_path(npk_fs_id id);

bool npk_fs_mount(npk_fs_id mountpoint, npk_handle fs_driver_id, npk_mount_options opts);
bool npk_fs_create(REQUIRED npk_fs_id* new_id, npk_fs_id dir, npk_fsnode_type type, npk_string name);
bool npk_fs_remove(npk_fs_id node);
bool npk_fs_find_child(REQUIRED npk_fs_id* found_id, npk_fs_id dir, npk_string name);

#ifdef __cplusplus
}
#endif
