#include <interfaces/driver/Events.h>
#include <interfaces/Helpers.h>
#include <debug/Log.h>
#include <tasking/Waitable.h>
#include <tasking/Threads.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Tasking;

    //static_assert(sizeof(npk_event) == sizeof(Waitable));
    static_assert(alignof(npk_event) == alignof(Waitable));
    //static_assert(sizeof(npk_wait_entry) == sizeof(WaitEntry));
    static_assert(alignof(npk_wait_entry) == alignof(WaitEntry));

    DRIVER_API_FUNC
    bool npk_wait_one(REQUIRED npk_event* event, REQUIRED npk_wait_entry* entry, npk_duration timeout)
    {
        VALIDATE_(event != nullptr, false);
        VALIDATE_(entry != nullptr, false);

        const sl::ScaledTime waitTime(static_cast<sl::TimeScale>(timeout.scale), timeout.ticks);
        return WaitOne(reinterpret_cast<Waitable*>(event), reinterpret_cast<WaitEntry*>(entry), waitTime);
    }

    DRIVER_API_FUNC
    size_t npk_wait_many(size_t count, REQUIRED npk_event** events, REQUIRED npk_wait_entry* entries, bool wait_all, npk_duration timeout)
    {
        VALIDATE_(count != 0, 0);
        VALIDATE_(events != nullptr, 0);
        VALIDATE_(entries != nullptr, 0);

        auto waitables = reinterpret_cast<Waitable**>(events);
        auto waiters = reinterpret_cast<WaitEntry*>(entries);
        const sl::ScaledTime waitTime(static_cast<sl::TimeScale>(timeout.scale), timeout.ticks);

        //return WaitMany({ waitables, count }, { waiters, count }, waitTime, wait_all);
        ASSERT_UNREACHABLE();
    }

    DRIVER_API_FUNC
    void npk_cancel_wait(npk_handle tid)
    {
        VALIDATE_(tid != NPK_INVALID_HANDLE,);

        Thread* t = ProgramManager::Global().GetThread(tid);
        VALIDATE_(t != nullptr,);
        CancelWait(t);
    }

    DRIVER_API_FUNC
    size_t npk_signal_event(REQUIRED npk_event* event, size_t count)
    {
        VALIDATE_(event != nullptr, 0);
        VALIDATE_(count != 0, 0);

        auto* waitable = reinterpret_cast<Waitable*>(event);
        return waitable->Signal(count);
    }

    DRIVER_API_FUNC
    void npk_reset_event(REQUIRED npk_event* event)
    {
        ASSERT_UNREACHABLE();
    }
}

