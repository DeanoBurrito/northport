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

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    npk_uint frequency;
    npk_uint ticks;
} npk_duration;

typedef enum
{
    npk_time_scale_nanos = 1'000'000'000,
    npk_time_scale_micros = 1'000'000,
    npk_time_scale_millis = 1'000,
    npk_time_scale_seconds = 1,
} npk_time_scale;

NPK_STRUCT_OPAQUE(clock_event);

npk_duration npk_get_monotonic_time();

void npk_queue_clock_event(NPK_REQUIRED npk_clock_event* ev);
npk_bool npk_try_dequeue_clock_event(NPK_REQUIRED npk_clock_event* ev);

#ifdef __cplusplus
}
#endif
