#include <core/RunLevels.h>
#include <arch/Misc.h>
#include <core/Log.h>
#include <core/Smp.h>

namespace Npk::Core
{
    constexpr const char* RunLevelStrs[] = 
    {
        "normal",
        "apc",
        "dpc",
        "clock",
        "interrupt"
    };

    static_assert(static_cast<size_t>(RunLevel::Normal) == 0);
    static_assert(static_cast<size_t>(RunLevel::Apc) == 1);
    static_assert(static_cast<size_t>(RunLevel::Dpc) == 2);
    static_assert(static_cast<size_t>(RunLevel::Clock) == 3);
    static_assert(static_cast<size_t>(RunLevel::Interrupt) == 4);

    const char* RunLevelName(RunLevel runLevel)
    {
        if (runLevel <= RunLevel::Interrupt)
            return RunLevelStrs[static_cast<size_t>(runLevel)];
        return "";
    }

    RunLevel RaiseRunLevel(RunLevel newRl)
    {
        const RunLevel prev = CoreLocal().runLevel;
        ASSERT_(newRl >= prev);
    }

    void LowerRunLevel(RunLevel newRl)
    {}

    void QueueDpc(DpcStore* dpc)
    {}

    void QueueApc(ApcStore* apc)
    {}

    void QueueRemoteDpc(size_t coreId, DpcStore* dpc)
    {
        ASSERT_UNREACHABLE();
    }
}
