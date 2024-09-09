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
        sl::SpinLock lock;
        size_t fullErrorCount;
        MailboxRpc callbacks[MailboxQueueDepth]; //TODO: lockless, no alloc, mpsc queue
    };

    sl::RwLock mailboxContainerLock;
    sl::Vector<IpiMailbox*> mailboxes;

    void InitIpiMailbox()
    {
        mailboxContainerLock.WriterLock();
        IpiMailbox& mailbox = *mailboxes.EmplaceAt(CoreLocal().id, new IpiMailbox);
        mailboxContainerLock.WriterUnlock();

        sl::ScopedLock mailboxLock(mailbox.lock);
        mailbox.fullErrorCount = 0;
        for (size_t i = 0; i < MailboxQueueDepth; i++)
            mailbox.callbacks[i].callback = nullptr;
    }

    void ProcessIpiMail()
    {
        mailboxContainerLock.ReaderLock();
        IpiMailbox& mailbox = *mailboxes[CoreLocal().id];
        mailboxContainerLock.ReaderUnlock();

        for (size_t i = 0; i < MailboxQueueDepth; i++)
        {
            if (mailbox.callbacks[i].callback == nullptr)
                continue;

            //acquire the mailbox lock for the smallest region possible,
            //we have no idea how long the callback will take.
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

        mailboxContainerLock.ReaderLock();
        IpiMailbox& mailbox = *mailboxes[dest];
        mailboxContainerLock.ReaderUnlock();

        mailbox.lock.Lock();
        for (size_t i = 0; i < MailboxQueueDepth; i++)
        {
            if (mailbox.callbacks[i].callback != nullptr)
                continue;
            
            mailbox.callbacks[i].arg = arg;
            mailbox.callbacks[i].callback = callback;
            SendIpi(dest);
            
            mailbox.fullErrorCount = 0;
            mailbox.lock.Unlock();
            return;
        }
        const bool emitError = (mailbox.fullErrorCount % EmitErrorOnCount) == 0;
        mailbox.fullErrorCount++;
        mailbox.lock.Unlock();

        if (emitError)
            Log("Mailbox queue for core %zu is full, mail dropped.", LogLevel::Error, dest);
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
