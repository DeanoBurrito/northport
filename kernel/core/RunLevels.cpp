#include <core/RunLevels.h>
#include <arch/Misc.h>
#include <arch/Interrupts.h>
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
        const bool restoreIntrs = InterruptsEnabled();
        DisableInterrupts();
        ASSERT_(newRl > CurrentRunLevel());

        const RunLevel prevRl = CurrentRunLevel();
        SetRunLevel(newRl);

        if (restoreIntrs && CurrentRunLevel() < RunLevel::Clock)
            EnableInterrupts();
        return prevRl;
    }

    void LowerRunLevel(RunLevel newRl)
    {
        const bool restoreIntrs = InterruptsEnabled();
        DisableInterrupts();
        ASSERT_(newRl < CurrentRunLevel());

        while (CurrentRunLevel() != newRl)
        {
            switch(CurrentRunLevel())
            {
            case RunLevel::Dpc:
                {
                    auto dpcs = CoreLocalDpcs();
                    if (restoreIntrs)
                        EnableInterrupts();

                    DpcStore* dpc = nullptr;
                    while ((dpc = dpcs->Pop()) != nullptr)
                    {
                        dpc->next = nullptr;
                        dpc->data.function(dpc->data.arg);
                    }
                    DisableInterrupts();
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

            SetRunLevel(static_cast<RunLevel>(static_cast<unsigned>(CurrentRunLevel()) - 1));
        }

        if (restoreIntrs && CurrentRunLevel() < RunLevel::Clock)
            EnableInterrupts();
    }

    void QueueDpc(DpcStore* dpc)
    {
        VALIDATE_(dpc != nullptr, );
        VALIDATE_(dpc->next == nullptr, );

        /* The dpc/apc queues are MPSC, so we can push atomicaly to them without
         * worrying about the current core's runlevel. Later on we raise the runlevel to
         * execute any dpcs if we're below the DPC runlevel.
         */
        CoreLocalDpcs()->Push(dpc);

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

        CoreLocalApcs()->Push(apc);

        if (CurrentRunLevel() >= RunLevel::Apc)
            return;
        const auto prevRl = CurrentRunLevel();
        RaiseRunLevel(RunLevel::Apc);
        LowerRunLevel(prevRl);
    }

    static void DpcOverIpiHandler(void* arg)
    { 
        return QueueDpc(static_cast<DpcStore*>(arg)); 
    }

    void QueueRemoteDpc(size_t coreId, DpcStore* dpc)
    {
        SendSmpMail(coreId, DpcOverIpiHandler, dpc);
    }
}
