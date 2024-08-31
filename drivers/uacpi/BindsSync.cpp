#include <uacpi/kernel_api.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/driver/Scheduling.h>
#include <interfaces/driver/Events.h>
#include <Log.h>
#include <Locks.h>

extern "C"
{
    uacpi_handle uacpi_kernel_create_mutex()
    {
        npk_event* event = new npk_event(); //events can act like a semaphore, which we can use as our mutex.
        npk_signal_event(event, 1); //start the event with a count of 1

        return event;
    }

    void uacpi_kernel_free_mutex(uacpi_handle handle)
    {
        delete static_cast<npk_event*>(handle);
    }

    uacpi_bool uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto event = static_cast<npk_event*>(handle);
        
        npk_wait_entry waitEntry {};
        npk_duration waitDuration { .scale = npk_time_scale::Millis, .ticks = timeout };
        if (timeout == 0xFFFF)
            waitDuration.ticks = -1ul;

        return npk_wait_one(event, &waitEntry, waitDuration) ? UACPI_TRUE : UACPI_FALSE;
    }

    void uacpi_kernel_release_mutex(uacpi_handle handle)
    {
        npk_signal_event(static_cast<npk_event*>(handle), 1);
    }

    uacpi_handle uacpi_kernel_create_event()
    {
        return new npk_event;
    }

    void uacpi_kernel_free_event(uacpi_handle handle)
    {
        delete static_cast<npk_event*>(handle);
    }

    uacpi_thread_id uacpi_kernel_get_thread_id()
    { 
        return reinterpret_cast<uacpi_thread_id>(npk_current_thread()); 
    }

    uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
    {
        auto event = static_cast<npk_event*>(handle);
        
        npk_wait_entry waitEntry {};
        npk_duration waitDuration { .scale = npk_time_scale::Millis, .ticks = timeout };
        if (timeout == 0xFFFF)
            waitDuration.ticks = -1ul;

        return npk_wait_one(event, &waitEntry, waitDuration) ? UACPI_TRUE : UACPI_FALSE;
    }

    void uacpi_kernel_signal_event(uacpi_handle handle)
    {
        npk_signal_event(static_cast<npk_event*>(handle), 1);
    }

    void uacpi_kernel_reset_event(uacpi_handle handle)
    {
        npk_reset_event(static_cast<npk_event*>(handle));
    }

    uacpi_handle uacpi_kernel_create_spinlock()
    {
        return new sl::SpinLock();
    }

    void uacpi_kernel_free_spinlock(uacpi_handle handle)
    {
        delete static_cast<sl::SpinLock*>(handle);
    }

    uacpi_cpu_flags uacpi_kernel_spinlock_lock(uacpi_handle handle)
    {
        sl::SpinLock* lock = static_cast<sl::SpinLock*>(handle);
        ASSERT_(lock != nullptr);

        uacpi_cpu_flags flags = -1ul;
        npk_runlevel prevRl;
        if (npk_ensure_runlevel(npk_runlevel::Interrupt, &prevRl))
            flags = prevRl;
        lock->Lock();
        return flags;
    }

    void uacpi_kernel_spinlock_unlock(uacpi_handle handle, uacpi_cpu_flags flags)
    {
        sl::SpinLock* lock = static_cast<sl::SpinLock*>(handle);
        ASSERT_(lock != nullptr);

        lock->Unlock();
        if (flags != -1ul)
            npk_lower_runlevel(static_cast<npk_runlevel>(flags));
    }
}
