#include <core/Smp.h>
#include <core/Log.h>
#include <hardware/Platform.h>
#include <Maths.h>

namespace Npk::Core
{
    constexpr size_t PanicWaitCycles = 0x800'0000;

    SmpInfo smpInfo;

    bool MailToOne(CpuId who, MailboxEntry mail, bool urgent)
    {
        return MailToMany({ &who, 1 }, mail, urgent);
    }

    void DispatchIpi();

    bool MailToMany(sl::Span<CpuId> who, MailboxEntry mail, bool urgent)
    {
        ASSERT(urgent == false, "TODO: not implemented");

        size_t failCount = 0;
        bool selfMail = false;

        for (size_t i = 0; i < who.Size(); i++)
        {
            auto control = static_cast<MailboxControl*>(GetPerCpu(who[i], smpInfo.offsets.mailbox));

            control->lock.Lock();
            auto entry = control->free.PopFront();
            control->lock.Unlock();

            if (entry == nullptr)
            {
                failCount++;
                continue;
            }

            *entry = mail;
            control->lock.Lock();
            control->pending.PushBack(entry);
            control->lock.Unlock();

            if (who[i] == CoreId())
                selfMail = true;
            else
                SendIpi(who[i], urgent);
        }

        if (selfMail)
        {
            auto prevRl = RaiseRunLevel(RunLevel::Interrupt);
            DispatchIpi();
            LowerRunLevel(prevRl);
        }

        return failCount == 0;
    }

    bool MailToSet(CpuBitset who, MailboxEntry mail, bool urgent)
    {
        CpuId ids[sizeof(CpuBitset) * 8];
        size_t idsNextSlot = 0;

        for (size_t i = 0; i < sizeof(CpuBitset); i++)
        {
            if ((who & (1ull << i)) != 0)
                ids[idsNextSlot++] = i;
        }

        return MailToMany({ ids, idsNextSlot }, mail, urgent);
    }

    bool MailToAll(MailboxEntry mail, bool urgent, bool includeSelf)
    {
        size_t failCount = 0;

        for (size_t i = 0; i < smpInfo.cpuCount; i++)
        {
            if (i == CoreId() && !includeSelf)
                continue;
            
            if (MailToOne(i, mail, urgent))
                continue;
            failCount++;
        }

        return failCount == 0;
    }

    sl::Atomic<CpuBitset> pendingPanics;

    void PanicAllCores()
    {
        pendingPanics.SetBits(static_cast<CpuBitset>(~0));

        for (size_t i = 0; i < smpInfo.cpuCount; i++)
        {
            auto control = static_cast<MailboxControl*>(GetPerCpu(i, smpInfo.offsets.mailbox));
            control->remotePanic = 1;
            SendIpi(i, true);
        }

        while (pendingPanics != 0)
        {
            for (size_t i = 0; i < PanicWaitCycles; i++)
                sl::HintSpinloop();

            Log("Panic sequence stalled, waiting on other cpus to ack: 0x%tu",
                LogLevel::Info, pendingPanics.Load());
        }
    }
}

namespace Npk
{
    using namespace Core;

    void DispatchIpi()
    {
        ASSERT_(CurrentRunLevel() == RunLevel::Interrupt);

        auto control = static_cast<MailboxControl*>(GetMyPerCpu(smpInfo.offsets.mailbox));
        if (control->remotePanic)
        {
            pendingPanics.ClearBits(1 << CoreId());
            Halt();
        }

        while (true)
        {
            control->lock.Lock();
            auto entry = control->pending.PopFront();
            control->lock.Unlock();
            
            if (entry == nullptr)
                break;

            entry->callback(entry->arg);
            if (entry->onComplete != nullptr)
                entry->onComplete->Notify();

            control->lock.Lock();
            control->free.PushBack(entry);
            control->lock.Unlock();
        }
    }
}
