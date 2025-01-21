#include <core/Smp.h>
#include <arch/Hat.h>
#include <arch/Interrupts.h>
#include <core/Pmm.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <Hhdm.h>
#include <containers/List.h>
#include <Locks.h>

namespace Npk::Core
{
    struct MailboxEntry
    {
        SmpMailCallback callback;
        void* arg;
    };

    struct MailboxControl
    {
        sl::FwdListHook hook;

        size_t id;
        ShootdownQueue tlbEvictions;
        sl::Atomic<bool> shouldPanic;
        sl::SpinLock entriesLock; //TODO: switch to lockfree ringbuffer
        sl::Span<MailboxEntry> entries;
    };

    sl::RwLock mailboxesLock;
    sl::FwdList<MailboxControl, &MailboxControl::hook> mailboxes;
    sl::Atomic<size_t> smpPendingPanics = 0;

    void InitLocalSmpMailbox()
    {
        MailboxControl* control = NewWired<MailboxControl>();
        ASSERT_(control != nullptr);

        auto maybePm = PmAlloc();
        ASSERT_(maybePm.HasValue());

        const size_t entryCount = PageSize() / sizeof(MailboxEntry);
        MailboxEntry* entries = reinterpret_cast<MailboxEntry*>(*maybePm + hhdmBase);
        control->entries = sl::Span<MailboxEntry>(entries, entryCount);
        for (size_t i = 0; i < control->entries.Size(); i++)
            control->entries[i].callback = nullptr;

        control->id = CoreLocalId();
        SetLocalPtr(SubsysPtr::IpiMailbox, control);

        mailboxesLock.WriterLock();
        mailboxes.PushBack(control);
        smpPendingPanics++;
        mailboxesLock.WriterUnlock();

        Log("Smp mailbox created: %p, entries=%zu", LogLevel::Verbose, control, entryCount);
    }

    void ProcessLocalMail()
    {
        ASSERT_(CurrentRunLevel() == RunLevel::Interrupt);
        auto* mailbox = static_cast<MailboxControl*>(GetLocalPtr(SubsysPtr::IpiMailbox));
        if (mailbox->shouldPanic)
        {
            smpPendingPanics--;
            Halt();
        }

        mailbox->entriesLock.Lock();
        for (size_t i = 0; i < mailbox->entries.Size(); i++)
        {
            mailbox->entries[i].callback(mailbox->entries[i].arg);
            mailbox->entries[i].callback = nullptr;
        }
        mailbox->entriesLock.Unlock();

        TlbShootdown* evict = nullptr;
        while ((evict = mailbox->tlbEvictions.Pop()) != nullptr)
        {
            for (size_t i = 0; i < evict->data.length; i += PageSize())
                HatFlushMap(evict->data.base + i);

            if (--evict->data.pending == 0 && evict->data.onComplete != nullptr)
                QueueDpc(evict->data.onComplete);
        }
    }

    static MailboxControl* FindMailbox(size_t id)
    {
        MailboxControl* found = nullptr;
        mailboxesLock.ReaderLock();

        for (auto it = mailboxes.Begin(); it != mailboxes.End(); ++it)
        {
            if (it->id != id)
                continue;
            found = &*it;
            break;
        }
        
        mailboxesLock.ReaderUnlock();
        return found;
    }

    void SendSmpMail(size_t destCore, SmpMailCallback callback, void* arg)
    {
        VALIDATE_(callback != nullptr, );
        MailboxControl* dest = FindMailbox(destCore);
        VALIDATE_(dest != nullptr, );

        sl::ScopedLock scopeLock(dest->entriesLock);
        for (size_t i = 0; i < dest->entries.Size(); i++)
        {
            if (dest->entries[i].callback != nullptr)
                continue;

            dest->entries[i].callback = callback;
            dest->entries[i].arg = arg;
            SendIpi(destCore, false);
            return;
        }

        Log("Failed to send mail to core %zu, mailbox full.", LogLevel::Fatal, destCore);
    }

    void SendShootdown(size_t destCore, TlbShootdown* shootdown)
    {
        VALIDATE_(shootdown != nullptr, );
        MailboxControl* dest = FindMailbox(destCore);
        VALIDATE_(dest != nullptr, );
        
        dest->tlbEvictions.Push(shootdown);
    }

    void PanicAllCores()
    {
        mailboxesLock.ReaderLock();
        for (auto it = mailboxes.Begin(); it != mailboxes.End(); ++it)
        {
            if (it->id == CoreLocalId())
                continue;

            it->shouldPanic = true;
            SendIpi(it->id, true);
        }
        mailboxesLock.ReaderUnlock();

        while (smpPendingPanics != 1)
            sl::HintSpinloop();
    }
}
