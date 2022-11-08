#include <interrupts/Ipi.h>
#include <containers/Vector.h>
#include <containers/LinkedList.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    constexpr size_t MailboxQueueDepth = 0x1F;
    
    struct MailboxRpc
    {
        void (*callback)(void*);
        void* arg;
    };
    
    struct IpiMailbox
    {
        InterruptLock lock;
        MailboxRpc callbacks[MailboxQueueDepth];
    };

    sl::Vector<IpiMailbox> mailboxes;

    void InitIpiMailbox()
    {
        mailboxes.EmplaceAt(CoreLocal().id);
    }

    void ProcessIpiMail()
    {
        IpiMailbox& mailbox = mailboxes[CoreLocal().id];

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
            i = (size_t)-1;
        }
    }

    void SendIpiMail(size_t dest, void (*callback)(void*), void* arg)
    {
        ASSERT(dest < mailboxes.Size(), "IPI mailbox does not exist");

        IpiMailbox& mailbox = mailboxes[dest];
        sl::ScopedLock scopeLock(mailbox.lock);

        for (size_t i = 0; i < MailboxQueueDepth; i++)
        {
            if (mailbox.callbacks[i].callback != nullptr)
                continue;
            
            mailbox.callbacks[i].arg = arg;
            mailbox.callbacks[i].callback = callback;
            SendIpi(dest);
            return;
        }

        Log("Mailbox queue for core %lu is full, mail dropped.", LogLevel::Error, dest);
    }
}
