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

#include "Primitives.h"
#include "Time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    void* reserved[4];
} npk_event;

typedef struct
{
    void* reserved[6];
} npk_wait_entry;

bool npk_wait_one(REQUIRED npk_event* event, REQUIRED npk_wait_entry* entry, npk_duration timeout);
size_t npk_wait_many(size_t count, REQUIRED npk_event** events, REQUIRED npk_wait_entry* entries, bool wait_all, npk_duration timeout);
void npk_cancel_wait(npk_handle tid);
size_t npk_signal_event(REQUIRED npk_event* event, size_t count);
void npk_reset_event(REQUIRED npk_event* event);

#ifdef __cplusplus
}
#endif
