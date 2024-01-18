#include <debug/Log.h>
#include <interfaces/driver/Scheduling.h>
#include <tasking/Thread.h>

extern "C"
{
    using namespace Npk::Tasking;

    [[gnu::used]]
    void npk_thread_exit(size_t code)
    {
        Thread::Current().Exit(code);
    }
}
