#include <interrupts/Ipi.h>
#include <debug/Log.h>
#include <containers/Vector.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    constexpr size_t MailboxQueueDepth = 0x40;
    constexpr size_t EmitErrorOnCount = 50;
    
    struct MailboxRpc
    {
        void (*callback)(void*);
        void* arg;
    };
    
    struct IpiMailbox
    {
        sl::InterruptLock lock;
        size_t fullErrorCount;
        MailboxRpc callbacks[MailboxQueueDepth];
    };

    sl::InterruptLock mailboxContainerLock;
    sl::Vector<IpiMailbox*> mailboxes;

    void InitIpiMailbox()
    {
        sl::ScopedLock scopeLock(mailboxContainerLock);

        IpiMailbox& mailbox = *mailboxes.EmplaceAt(CoreLocal().id, new IpiMailbox);
        mailbox.fullErrorCount = 0;
    }

    void ProcessIpiMail()
    {
        IpiMailbox& mailbox = *mailboxes[CoreLocal().id];

        for (size_t i = 0; i < MailboxQueueDepth; i++)
        {
            if (mailbox.callbacks[i].callback == nullptr)
                continue;

            //only acquire the lock while we read the mailbox, this allows other cores to post mail while the callback runs.
            mailbox.lock.Lock();
            auto callback = mailbox.callbacks[i].callback;
            void* arg = mailbox.callbacks[i].arg;
            mailbox.callbacks[i].callback = nullptr;
            mailbox.lock.Unlock();

            callback(arg);
        }
    }

    void SendIpiMail(size_t dest, void (*callback)(void*), void* arg)
    {
        ASSERT(dest < mailboxes.Size(), "IPI mailbox does not exist");

        IpiMailbox& mailbox = *mailboxes[dest];
        sl::ScopedLock scopeLock(mailbox.lock);

        for (size_t i = 0; i < MailboxQueueDepth; i++)
        {
            if (mailbox.callbacks[i].callback != nullptr)
                continue;
            
            mailbox.callbacks[i].arg = arg;
            mailbox.callbacks[i].callback = callback;
            SendIpi(dest);
            
            mailbox.fullErrorCount = 0;
            return;
        }

        if ((mailbox.fullErrorCount % EmitErrorOnCount) == 0)
            Log("Mailbox queue for core %zu is full, mail dropped.", LogLevel::Error, dest);
        mailbox.fullErrorCount++;
    }

    void PanicIpiHandler(void*)
    { Halt(); }
    
    void BroadcastPanicIpi()
    {
        for (size_t i = 0; i < mailboxes.Size(); i++)
        {
            if (mailboxes[i] == nullptr || i == CoreLocal().id)
                continue;
            
            sl::ScopedLock coreLock(mailboxes[i]->lock);

            //because of the severity of a panic, we overwrite *every* mailbox entry in the destination core.
            //this does cause other mail to be lost, but this ensures that the panic is received asap.
            for (size_t m = 0; m < MailboxQueueDepth; m++)
                mailboxes[i]->callbacks[m].callback = PanicIpiHandler;
            SendIpi(i);
        }
    }
}
