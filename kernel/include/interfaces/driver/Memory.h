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

typedef struct
{
    uintptr_t lower_limit;
    uintptr_t upper_limit;
} npk_pm_limits;

typedef enum
{
    VmWrite = 1 << 0,
    VmExecute = 1 << 1,
    VmUser = 1 << 2,
    VmGuarded = 1 << 3,

    VmAnon = 1 << 24,
    VmMmio = 1 << 25,
    VmFile = 1 << 26
} npk_vm_flags;

#define NPK_VM_FLAG_TYPE_MASK (0xFF << 24)

typedef struct
{
    uintptr_t lower_limit;
    uintptr_t upper_limit;
    size_t alignment;
} npk_vm_limits;

typedef enum
{
   NoDeferBacking = 1 << 1,
} npk_file_vm_flags;

typedef struct
{
    npk_string filepath;
    size_t file_offset;
    npk_file_vm_flags flags;
} npk_file_vm_arg;

typedef struct
{
    size_t length;
    uintptr_t phys_base;
} npk_mdl_ptr;

typedef struct
{
    void* addr_space;
    size_t length;
    uintptr_t virt_base;
    size_t ptr_count;
    npk_mdl_ptr* ptrs;
} npk_mdl;

uintptr_t npk_hhdm_base();
size_t npk_hhdm_limit();

size_t npk_pm_alloc_size();
uintptr_t npk_pm_alloc(OPTIONAL npk_pm_limits* limits);
uintptr_t npk_pm_alloc_many(size_t count, OPTIONAL npk_pm_limits* limits);
bool npk_pm_free(uintptr_t paddr);
bool npk_pm_free_many(uintptr_t paddr, size_t count);

void* npk_vm_alloc(size_t length, void* arg, npk_vm_flags flags, OPTIONAL npk_vm_limits* limits);
bool npk_vm_acquire_mdl(REQUIRED npk_mdl* mdl, void* vaddr, size_t length);
void npk_vm_release_mdl(void* vaddr);
bool npk_vm_free(void* vm_ptr);
bool npk_vm_get_flags(void* vm_ptr, REQUIRED npk_vm_flags* flags);
bool npk_vm_set_flags(void* vm_ptr, npk_vm_flags flags);
bool npk_vm_split(void* vm_ptr, REQUIRED uintptr_t* offset);

void* npk_heap_alloc(size_t count);
void npk_heap_free(void* ptr, size_t count);

#ifdef __cplusplus
}
#endif
