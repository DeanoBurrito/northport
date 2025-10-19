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
     * interrupts disabled. Informs the kernel that a page fault occured at
     * passive IPL.
     */
    void DispatchPageFault(uintptr_t addr, bool write);

    /* This should be called after hardware-specific code has brought-up a
     * new (from the system's perspective) cpu. It allows the rest of the
     * kernel to make use of this cpu, but does not relinquish control
     * (i.e. a context switch wont be triggered).
     */
    void BringCpuOnline(ThreadContext* idle);
}
