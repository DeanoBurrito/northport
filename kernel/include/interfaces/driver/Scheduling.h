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
    OWNING npk_string name;
} npk_process_create_args;

typedef struct
{
    size_t affinity;
    size_t stack_size;
    OWNING npk_string name;
    OPTIONAL void* start_arg;
} npk_thread_create_args;

typedef enum
{
    Setup = 0,
    Dead = 1,
    Ready = 2,
    Queued = 3,
    Running = 4,
} npk_thread_state;

npk_handle npk_create_process(OPTIONAL npk_process_create_args* args);
npk_handle npk_create_thread(npk_handle process_id, uintptr_t entry, OPTIONAL npk_thread_create_args* args);

npk_handle npk_current_thread();
npk_handle npk_current_process();
npk_handle npk_kernel_process();
bool npk_get_thread_state(npk_handle tid, REQUIRED npk_thread_state* state);
bool npk_get_thread_affinity(npk_handle tid, REQUIRED size_t* affinity);

void npk_thread_start(npk_handle tid, OPTIONAL void* arg);
void npk_thread_exit(size_t code);
void npk_thread_kill(npk_handle tid, size_t code);
void npk_thread_join(npk_handle tid);

#ifdef __cplusplus
}
#endif
