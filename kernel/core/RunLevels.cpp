#include <core/RunLevels.h>
#include <hardware/Arch.h>
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
        const RunLevel prevRl = ExchangeRunLevel(newRl);
        ASSERT_(newRl > prevRl);

        if (newRl >= RunLevel::Clock)
            DisableIntrs();

        return prevRl;
    }

    void LowerRunLevel(RunLevel newRl)
    {
        const bool restoreIntrs = DisableIntrs();
        ASSERT_(newRl < CurrentRunLevel());

        while (CurrentRunLevel() != newRl)
        {
            switch(CurrentRunLevel())
            {
            case RunLevel::Dpc:
                {
                    auto dpcs = LocalDpcs();
                    if (restoreIntrs)
                        EnableIntrs();

                    DpcStore* dpc = nullptr;
                    while ((dpc = dpcs->Pop()) != nullptr)
                    {
                        dpc->next = nullptr;
                        dpc->data.function(dpc->data.arg);
                    }
                    DisableIntrs();
                    break;
                }
            case RunLevel::Apc: //TODO: APCs
                break;
            case RunLevel::Normal:
                //TODO: inform scheduler it can switch now if it desires
                break;
            default: 
                break;
            }

            ExchangeRunLevel(static_cast<RunLevel>(static_cast<unsigned>(CurrentRunLevel()) - 1));
        }

        if (restoreIntrs && CurrentRunLevel() < RunLevel::Clock)
            EnableIntrs();
    }

    void QueueDpc(DpcStore* dpc)
    {
        VALIDATE_(dpc != nullptr, );
        VALIDATE_(dpc->next == nullptr, );

        /* The dpc/apc queues are MPSC, so we can push atomicaly to them without
         * worrying about the current core's runlevel. Later on we raise the runlevel to
         * execute any dpcs if we're below the DPC runlevel.
         */
        LocalDpcs()->Push(dpc);

        if (CurrentRunLevel() >= RunLevel::Dpc)
            return;
        const auto prevRl = CurrentRunLevel();
        RaiseRunLevel(RunLevel::Dpc);
        LowerRunLevel(prevRl);
    }

    void QueueApc(ApcStore* apc)
    {
        VALIDATE_(apc != nullptr, );
        VALIDATE_(apc->next == nullptr, );

        LocalApcs()->Push(apc);

        if (CurrentRunLevel() >= RunLevel::Apc)
            return;
        const auto prevRl = CurrentRunLevel();
        RaiseRunLevel(RunLevel::Apc);
        LowerRunLevel(prevRl);
    }
}
