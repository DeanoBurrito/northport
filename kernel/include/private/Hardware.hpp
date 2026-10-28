#pragma once

#include <Hardware.hpp>

namespace Npk
{
    /* Upcall into generic part of kernel, happens at interrupt IPL and with
     * interrupts disabled. Informs the kernel that the alarm expired based
     * on the last call to `ArmAlarm()`.
     */
    void DispatchAlarm();

    /* Upcall into generic part of kernel, happens at interrupt IPL and with
     * interrupts disabled. Notifies the kernel that another cpu wants
     * the attention of this one.
     */
    void DispatchIpi();

    /* Upcall into generic part of kernel, happens at interrupt IPL and with
     * interrupts disabled. Called when a device interrupt is fired,
     * `handle` contains the opaque handle the kernel provided when
     * the interrupt was allocated.
     */
    void DispatchInterrupt(size_t handle);

    /* Upcall into generic part of kernel, happens at interrupt IPL and with
     * interrupts disabled. Informs the kernel that a page fault occurred at
     * passive IPL.
     * If `probe` is non-null this fault should direct all failure paths to
     * set `*probe` instead of their usual behaviours, such that this fault is
     * otherwise unobservable (e.g. no signals fired at a process).
     */
    void DispatchPageFault(bool* probe, uintptr_t addr, bool write, bool user);
}
