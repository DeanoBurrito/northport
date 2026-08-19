#include <private/Core.hpp>
#include <Debugger.hpp>

namespace Npk
{
    constexpr sl::TimeCount IpiDebounceTime = 1_ms;
    constexpr sl::TimeCount FreezePingTime = 10_ms;
    constexpr CpuId FreezeTargetAll = static_cast<CpuId>(-1);

    static sl::Atomic<CpuId> freezeControl = 0;
    static sl::Atomic<CpuId> freezeCmdControl = 0;
    static CpuId freezeTarget;
    static void* freezeArg;
    static void (*freezeCommand)(void* arg);

    static inline SmpControl* GetControl(CpuId who)
    {
        auto& dom = MySystemDomain();
        who -= dom.smpBase;

        if (who >= dom.smpControls.Size())
            return nullptr;

        return &dom.smpControls[who];
    }

    static void HandleFreezing()
    {
        if (freezeControl.Load() == 0)
            return;

        //acknowledge that this cpu has been frozen
        freezeControl.Sub(1);

        //wait for the freeze-master to set the control to 0 (meaning thaw all
        //cpus). In the meantime check for any commands it wants us to run.
        bool commandRun = false;
        while (freezeControl.Load() != 0)
        {
            if (freezeCmdControl.Load(sl::Acquire) == 0)
            {
                commandRun = false;
                continue;
            }
            if (commandRun)
                continue;
            if (freezeTarget != MyCoreId() && freezeTarget != FreezeTargetAll)
                continue;

            freezeCommand(freezeArg);
            freezeCmdControl.Sub(1, sl::Release);
            commandRun = true;
        }
    }

    static CpuId TotalCpuCount()
    {
        //TODO: multi-domain support
        return MySystemDomain().smpControls.Size();
    }

    void DispatchIpi()
    {
        HandleFreezing();

        auto control = GetControl(MyCoreId());
        NPK_ASSERT(control != nullptr);

        SmpMail* mail = nullptr;
        while ((mail = control->mail.Pop()) != nullptr)
        {
            if (mail->function != nullptr)
                mail->function(mail->arg);
            if (mail->onComplete != nullptr)
                SetCondition(mail->onComplete);
        }

        control->status.lastIpi.Store({}, sl::Release);
        Private::ArmPendingRcuQuiesce();
    }

    RemoteCpuStatus* RemoteStatus(CpuId who)
    {
        auto control = GetControl(who);
        NPK_CHECK(control != nullptr, nullptr);

        return &control->status;
    }

    void SendMail(CpuId who, SmpMail* mail)
    {
        NPK_CHECK(mail != nullptr, );

        auto control = GetControl(who);
        NPK_CHECK(control != nullptr, );

        control->mail.Push(mail);
        NudgeCpu(who);
    }

    void NudgeCpu(CpuId who)
    {
        auto control = GetControl(who);
        NPK_CHECK(control != nullptr, );

        const auto lastIpi = control->status.lastIpi.Load(sl::Relaxed);
        auto nextIpi = IpiDebounceTime.Rebase(lastIpi.Frequency).ticks + lastIpi.epoch;
        if (nextIpi > GetMonotonicTime().epoch)
            return;

        sl::TimePoint expected { lastIpi };
        sl::TimePoint desired { GetMonotonicTime().epoch + IpiDebounceTime.ticks };
        if (control->status.lastIpi.CompareExchange(expected, desired, sl::Acquire))
            HwSendIpi(who);
    }

    size_t FreezeAllCpus(bool allowDefer)
    {
        const CpuId cpuCount = TotalCpuCount();

        CpuId expected = 0;
        CpuId desired = cpuCount;

        //try to become the freeze-master B)
        while (!freezeControl.CompareExchange(expected, desired))
        {
            //`freezeControl` was non-zero meaning another cpu has already
            //started a freeze. If `allowDefer` is set, we voluntarily freeze
            //the current cpu and allow the other cpu to continue its operation.
            //If `allowDefer` is cleared, we return 0 to the caller and it
            //handles the conflict.
            if (!allowDefer)
                return 0;

            HandleFreezing();
        }

        auto startTime = GetMonotonicTime();
        auto endTime = startTime.epoch;
        do
        {
            if (GetMonotonicTime().epoch >= endTime)
            {
                for (size_t i = 0; i < cpuCount; i++)
                    HwSendIpi(i);

                startTime = GetMonotonicTime();
                endTime = startTime.epoch;
                endTime += FreezePingTime.Rebase(startTime.Frequency).ticks;
            }
            
            sl::HintSpinloop();
        }
        while (freezeControl.Load() != 1);

        return cpuCount;
    }

    void ThawAllCpus()
    {
        CpuId expected = 1;
        CpuId desired = 0;

        //an atomic store would suffice, but the cmpexchg is a nice sanity
        //check.
        NPK_ASSERT(freezeControl.CompareExchange(expected, desired));
    }

    void RunOnFrozenCpus(void (*What)(void* arg), void* arg, bool includeSelf)
    {
        NPK_ASSERT(freezeControl.Load(sl::Acquire) == 1);
        NPK_ASSERT(freezeCmdControl.Load(sl::Acquire) == 0);

        freezeArg = arg;
        freezeCommand = What;
        freezeTarget = FreezeTargetAll;
        freezeCmdControl.Store(TotalCpuCount(), sl::Release);

        while (freezeCmdControl.Load(sl::Acquire) != 1)
            sl::HintSpinloop();

        if (includeSelf)
            What(arg);

        freezeCmdControl.Store(0, sl::Release);
    }

    void RunOnFrozenCpu(CpuId who, void (*What)(void* arg), void* arg)
    {
        NPK_ASSERT(freezeControl.Load(sl::Acquire) == 1);
        NPK_ASSERT(freezeCmdControl.Load(sl::Acquire) == 0);
        NPK_ASSERT(who < TotalCpuCount());

        freezeArg = arg;
        freezeCommand = What;
        freezeTarget = who;
        freezeCmdControl.Store(2, sl::Release);

        while (freezeCmdControl.Load(sl::Acquire) != 1)
            sl::HintSpinloop();

        freezeCmdControl.Store(0, sl::Release);
    }

    void SetCpuPerformanceData(CpuId who, uint8_t performance,
        uint8_t efficiency)
    {
        auto* status = RemoteStatus(who);
        if (status == nullptr)
            return;

        const uint8_t perf = performance;
        const uint8_t eff = efficiency;
        status->performanceCapacity.Exchange(performance, sl::AcqRel);
        status->efficiencyClass.Exchange(efficiency, sl::AcqRel);

        Log("Performance/efficiency update for cpu %zu: perf %u -> %u,\
            eff %u -> %u", LogLevel::Verbose, who, performance, perf,
            efficiency, eff);
    }
}

namespace Npk::Private
{
    CPU_LOCAL(uintptr_t, static myNodeLocals);

    void SetMyNodePointer(uintptr_t addr)
    {
        myNodeLocals = addr;
    }

    uintptr_t MyNodeLocals()
    {
        return *myNodeLocals;
    }
}
