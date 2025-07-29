#include <KernelApi.hpp>

namespace Npk
{
    constexpr sl::TimeCount IpiDebounceTime = 1_ms;

    static inline SmpControl* GetControl(CpuId who)
    {
        auto& dom = MySystemDomain();
        who -= dom.smpBase;

        if (who >= dom.smpControls.Size())
            return nullptr;

        return &dom.smpControls[who];
    }

    void DispatchIpi()
    {
        auto control = GetControl(MyCoreId());
        NPK_ASSERT(control != nullptr);

        SmpMail* mail = nullptr;
        while ((mail = control->mail.Pop()) != nullptr)
        {
            if (mail->data.function != nullptr)
                mail->data.function(mail->data.arg);
            if (mail->data.onComplete != nullptr)
                SignalWaitable(mail->data.onComplete);
        }

        RemoteFlushRequest* shootdown = nullptr;
        while ((shootdown = control->shootdowns.Pop()) != nullptr)
        {
            ArchFlushTlb(shootdown->data.base, shootdown->data.length);
            shootdown->data.acknowledgements.Sub(1, sl::Release);
        }

        control->status.lastIpi.Store({}, sl::Release);
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

    void FlushRemoteTlbs(sl::Span<CpuId> who, RemoteFlushRequest* what, bool sync)
    {
        NPK_CHECK(what != nullptr, );

        what->data.acknowledgements.Store(who.Size(), sl::Acquire);

        for (size_t i = 0; i < who.Size(); i++)
        {
            auto control = GetControl(who[i]);
            if (control == nullptr)
            {
                what->data.acknowledgements.Sub(1, sl::Release);
                continue;
            }

            control->shootdowns.Push(what);
            NudgeCpu(who[i]);
        }

        if (!sync)
            return;

        size_t count = 0;
        while (what->data.acknowledgements.Load(sl::Relaxed))
        {
            count++;
            if (count == 123456)
            {
                Log("Unusually long wait for tlb shootdown to complete: 0x%tx->0x%tx",
                    LogLevel::Warning, what->data.base, what->data.base + what->data.length);
                count = 0;
            }

            sl::HintSpinloop();
        }
    }

    void SetMyIpiId(void* id)
    {
        Log("Setting ipi id to %p", LogLevel::Verbose, id);

        auto control = GetControl(MyCoreId());
        NPK_CHECK(control != nullptr, );
        control->ipiId = id;
    }

    void* GetIpiId(CpuId id)
    {
        auto control = GetControl(id);
        if (control == nullptr)
            return nullptr;

        return control->ipiId;
    }

    void NudgeCpu(CpuId who)
    {
        auto control = GetControl(who);
        NPK_CHECK(control != nullptr, );

        const auto lastIpi = control->status.lastIpi.Load(sl::Relaxed);
        auto nextIpi = IpiDebounceTime.Rebase(lastIpi.Frequency).ticks + lastIpi.epoch;
        if (nextIpi > PlatReadTimestamp().epoch)
            return;

        sl::TimePoint expected { nextIpi };
        sl::TimePoint desired { PlatReadTimestamp().epoch + IpiDebounceTime.ticks };
        if (control->status.lastIpi.CompareExchange(expected, desired, sl::Acquire))
            PlatSendIpi(control->ipiId);
    }
}
