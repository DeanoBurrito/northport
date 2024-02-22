#include <debug/Log.h>
#include <interfaces/driver/Scheduling.h>
#include <interfaces/Helpers.h>
#include <tasking/Thread.h>

extern "C"
{
    using namespace Npk::Tasking;

    DRIVER_API_FUNC
    void npk_thread_exit(size_t code)
    {
        Thread::Current().Exit(code);
    }
}
