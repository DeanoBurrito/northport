#pragma once
#ifndef NP_SYSCALL_H
#define NP_SYSCALL_H

/* This file provides definitions for use with the northport kernel ABI, and
 * a small cross-platform API for interacting with the kernel and common
 * hardware.
 *
 * System Call ABI:
 * The full ABI is documented in the northport kernel manual, which can be build
 * from sources in the kernel repo. The wrapper function `np_do_syscall()` below
 * can be used as a portable alternative.
 * 
 * Portability:
 * This header contains architecture-specific assembly snippets to provide
 * convinient and portable wrappers for userspace software. However this means
 * each of these functions must be updated for each new architecture supported.
 * If you're here because you're porting northport, please follow the example
 * of previous ports and keep the code below tidy and organised.
 *
 * Licensing:
 * This file is part of the northport user API. It falls under the same license
 * as the rest of the kernel, which is included below for convinience.
 *
 *******************************************************************************
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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

enum np_status
{
    np_status_success = 0,
    np_status_invalid_arg = 1,
    np_status_bad_func = 2,
    np_status_invalid_system = 3,
};

enum np_sys_func
{
    np_sys_func_invalid = 0,
    np_sys_func_debug_log = 1,
};

/* This struct is mapped readable into every user address space, and writable
 * by the kernel. It allows user programs to query selected info without
 * needing to perform a syscall.
 */
typedef struct
{
    /* Current version of this struct. The current version is 1.
     */
    uint64_t version;

    /* Since version 1: 
     * The number of nanoseconds since the system powered on.
     * This is updated by the kernel on a best-effort basis and so it may lag
     * slightly at times. It is monotonic and synchronized across all cpus
     * in the same domain.
     */
    uint64_t uptime_ns;

    /* Since version 1:
     * This field and `timer_frequency` provide information to convert ticks
     * of the architectural timestamp counter to a more useful format. If the
     * current system does not provide a timestamp counter, `timer_frequency`
     * will be 0. The offset field treats 0 as a valid value, so it should
     * not be checked in this way.
     *
     * Example code for converting a raw timestamp value to nanoseconds is
     * provided below. It can be easily adjust to convert to other units of
     * time by changing the `nanos = 100000000` constant, which represents the
     * ratio of the desired unit to seconds.
     *
     * uint64_t
     * get_nanos_since_boot()
     * {
     *     const uint64_t nanos = 1000000000;
     *
     *     uint64_t value;
     *     if (np_read_timestamp(&value) != np_status_success)
     *         return -1;
     *
     *     value = value - timer_offset;
     *     uint64_t q = value / timer_frequency;
     *     uint64_t r = value % timer_frequency;
     *
     *     uint64_t a = r * nanos;
     *     a = a + (timer_frequency / 2);
     *     a = a / timer_frequency;
     *     a = a + (q * nanos);
     *
     *     return a;
     * }
     */
    uint64_t timer_offset;

    /* Since version 1:
     * Provides the frequency of the timestamp counter. See the description of
     * `time_offset` above for more details.
     */
    uint64_t timer_frequency;
} np_shared_data;

inline
np_status
np_read_timestamp(uint64_t* value)
{
    np_status status = np_status_invalid_system;
    
    if (value == NULL)
    {
        status = np_status_invalid_arg;
        return status;
    }

#ifdef __x86_64__
    uint64_t low;
    uint64_t high;
    asm("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");

    *value = low | (high << 32);
    status = np_status_success;

#else
#error "np_read_timestamp(): Unsupported architecture."
#endif

    return status;
}

inline
np_status
np_do_syscall(
    unsigned function,
    void* args,
    unsigned args_len
    )
{
    np_status status = np_status_bad_func;

#ifdef __x86_64__
    asm("\
        push %%r15; \
        push %%r14; \
        push %%r13; \
        push %%r12; \
        push %%rbp; \
        push %%rbx; \
        syscall; \
        pop %%rbx; \
        pop %%rbp; \
        pop %%r12; \
        pop %%r13; \
        pop %%r14; \
        pop %%r15; \
        "
        : "=a"(status)
        : "0"(function), "D"(args), "S"(args_len)
        : "memory");

#else
#error "np_do_syscall(): Unsupported architecture."
#endif

    return status;
}

#ifdef __cplusplus
}
#endif

#endif
