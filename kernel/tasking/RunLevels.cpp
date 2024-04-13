#include <tasking/RunLevels.h>
#include <tasking/Scheduler.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <interrupts/Ipi.h>

namespace Npk::Tasking
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

    const char* GetRunLevelName(RunLevel runLevel)
    {
        if (runLevel <= RunLevel::Interrupt)
            return RunLevelStrs[static_cast<size_t>(runLevel)];
        return "";
    }

    RunLevel RaiseRunLevel(RunLevel newLevel)
    {
        const RunLevel prev = CoreLocal().runLevel;
        ASSERT_(newLevel > prev);

        SetHardwareRunLevel(newLevel);
        CoreLocal().runLevel = newLevel;
        return prev;
    }

    sl::Opt<RunLevel> EnsureRunLevel(RunLevel level)
    {
        if (CoreLocal().runLevel < level)
            return RaiseRunLevel(level);
        return {};
    }

    void LowerRunLevel(RunLevel newLevel)
    {
        ASSERT_(newLevel < CoreLocal().runLevel);

        bool restoreInterrupts = false;
        bool doThreadSwitch = false;
        while (CoreLocal().runLevel > newLevel)
        {
            //we're aiming to drop the run level by 1, ensure that any queued operations
            //for the current level have been completed before we do this
            switch (CoreLocal().runLevel)
            {
            case RunLevel::Dpc:
                {
                    DpcStore* dpc = nullptr;
                    while ((dpc = CoreLocal().dpcs.Pop()) != nullptr)
                    {
                        dpc->next = nullptr;
                        dpc->data.function(dpc->data.arg);
                    }
                    break;
                }
            case RunLevel::Apc: //TOOD: implement APCs
            case RunLevel::Normal:
                if (Scheduler::Global().SwitchPending())
                {
                    doThreadSwitch = true;
                    restoreInterrupts = InterruptsEnabled();
                    DisableInterrupts();
                }
            default: break;
            }

            CoreLocal().runLevel--;
            SetHardwareRunLevel(CoreLocal().runLevel);
        }

        if (doThreadSwitch)
        {
            Scheduler::Global().DoPendingSwitch();
            if (restoreInterrupts)
                EnableInterrupts();
        }
    }

    void QueueDpc(DpcStore* dpc)
    {
        VALIDATE_(dpc != nullptr, );
        VALIDATE(dpc->next == nullptr,, "DpcStore already in use");

        CoreLocal().dpcs.Push(dpc);

        if (CoreLocal().runLevel >= RunLevel::Dpc)
            return;
        const RunLevel currLevel = CoreLocal().runLevel;
        RaiseRunLevel(RunLevel::Dpc);
        LowerRunLevel(currLevel);
    }

    void QueueApc(ApcStore* apc)
    {
        VALIDATE_(apc != nullptr, );
        VALIDATE(apc->next == nullptr,, "ApcStore already in use");

        CoreLocal().apcs.Push(apc);

        if (CoreLocal().runLevel >= RunLevel::Apc)
            return;
        const RunLevel currLevel = CoreLocal().runLevel;
        RaiseRunLevel(RunLevel::Apc);
        LowerRunLevel(currLevel);
    }

    static void DpcOverIpiHandler(void* arg)
    { return QueueDpc(static_cast<DpcStore*>(arg)); }

    void QueueRemoteDpc(size_t coreId, DpcStore* dpc)
    {
        if (coreId == CoreLocal().id)
            return QueueDpc(dpc);
        Interrupts::SendIpiMail(coreId, DpcOverIpiHandler, dpc);
    }
};
