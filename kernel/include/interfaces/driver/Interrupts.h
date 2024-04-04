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
    Normal = 0,
    Apc = 1,
    Dpc = 2,
    Clock = 3,
    Interrupt = 4,
} npk_runlevel;

typedef struct
{
    void* reserved;
    void (*function)(void* arg);
    void* arg;
} npk_dpc;

typedef struct
{
    void* reserved;
    void (*function)(void* arg);
    void* arg;
    npk_handle thread_id;
} npk_apc;

typedef struct
{
    void* reserved[8];

    void* callback_arg;
    bool (*callback)(void* arg);
    OPTIONAL npk_dpc* dpc;
} npk_interrupt_route;

npk_runlevel npk_raise_runlevel(npk_runlevel rl);
void npk_lower_runlevel(npk_runlevel rl);
void npk_queue_dpc(npk_dpc* dpc);
void npk_queue_apc(npk_apc* apc);

bool npk_add_interrupt_route(npk_interrupt_route* route, npk_handle core);
bool npk_claim_interrupt_route(npk_interrupt_route* route, npk_handle core, size_t vector);
bool npk_remove_interrupt_route(npk_interrupt_route* route);

#ifdef __cplusplus
}
#endif
