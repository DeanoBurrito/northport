#include <debug/Log.h>
#include <interfaces/driver/Scheduling.h>
#include <interfaces/Helpers.h>
#include <tasking/Threads.h>

extern "C"
{
    using namespace Npk::Tasking;

    static_assert((size_t)ThreadState::Setup == npk_thread_state::Setup);
    static_assert((size_t)ThreadState::Dead == npk_thread_state::Dead);
    static_assert((size_t)ThreadState::Ready == npk_thread_state::Ready);
    static_assert((size_t)ThreadState::Queued == npk_thread_state::Queued);
    static_assert((size_t)ThreadState::Running == npk_thread_state::Running);

    DRIVER_API_FUNC
    npk_handle npk_create_process(OPTIONAL npk_process_create_args* args)
    {
        if (args != nullptr)
            Log("TODO: npk_create_process() args support", LogLevel::Error);

        auto result = ProgramManager::Global().CreateProcess();
        return result.HasValue() ? *result : NPK_INVALID_HANDLE;
    }

    DRIVER_API_FUNC
    npk_handle npk_create_thread(npk_handle process_id, uintptr_t entry, OPTIONAL npk_thread_create_args* args)
    {
        ASSERT_UNREACHABLE();
    }

    DRIVER_API_FUNC
    npk_handle npk_current_thread()
    {
        return Thread::Current().Id();
    }

    DRIVER_API_FUNC
    npk_handle npk_current_process()
    {
        return Process::Current().Id();
    }

    DRIVER_API_FUNC
    npk_handle npk_kernel_process()
    {
        return Process::Kernel().Id();
    }

    DRIVER_API_FUNC
    bool npk_get_thread_state(npk_handle tid, REQUIRED npk_thread_state* state)
    {
        VALIDATE_(state != nullptr, false);

        auto thread = ProgramManager::Global().GetThread(tid);
        VALIDATE_(thread != nullptr, false);
        *state = static_cast<npk_thread_state>(thread->State());
        return true;
    }

    DRIVER_API_FUNC
    bool npk_get_thread_affinity(npk_handle tid, REQUIRED size_t* affinity)
    {
        VALIDATE_(affinity != nullptr, false);

        auto thread = ProgramManager::Global().GetThread(tid);
        VALIDATE_(thread != nullptr, false);
        *affinity = thread->GetAffinity();
        return true;
    }

    DRIVER_API_FUNC
    void npk_thread_start(npk_handle tid, OPTIONAL void* arg)
    {
        auto thread = ProgramManager::Global().GetThread(tid);
        VALIDATE_(thread != nullptr, );
        VALIDATE_(thread->State() == ThreadState::Setup, );

        thread->Start(arg);
    }

    DRIVER_API_FUNC
    void npk_thread_exit(size_t code)
    {
        Thread::Current().Exit(code);
    }

    DRIVER_API_FUNC
    void npk_thread_kill(npk_handle tid, size_t code)
    {
        auto thread = ProgramManager::Global().GetThread(tid);
        VALIDATE_(thread != nullptr, );

        thread->Exit(code);
    }

    void npk_thread_join(npk_handle tid)
    {
        ASSERT_UNREACHABLE();
    }
}
