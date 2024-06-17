#include <arch/Platform.h>
#include <arch/Timers.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <tasking/Clock.h>

namespace Npk
{
    void InitCore(size_t coreId)
    {}

    void ThreadedArchInit()
    {} //no-op
}

extern "C"
{
    void KernelEntry()
    {
        using namespace Npk;

        InitEarlyPlatform();
#ifdef NP_M68K_ASSUME_SERIAL
#endif

        InitMemory();
        InitPlatform();
        InitTimers();

        if (Boot::smpRequest.response != nullptr)
        { ASSERT_UNREACHABLE(); } //sounds exciting, I didnt know these existed
        else
            InitCore(0);

        Tasking::StartSystemClock();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }
}
