#include <IoPrivate.hpp>
#include <Core.hpp>
#include <CppUtils.hpp>

namespace Npk
{
    CPU_LOCAL(IopQueue, static localContinuations);
    CPU_LOCAL(IplSpinLock<Ipl::Dpc>, static localContsLock);

    void Private::QueueContinuation(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, );
        NPK_CHECK(packet->Continuation != nullptr, );
        AssertIpl(Ipl::Passive);

        sl::ScopedLock queueLock(*localContsLock);
        localContinuations->PushBack(packet);
    }

    void Private::RunPendingIopContinuations()
    {
        AssertIpl(Ipl::Passive);

        IopQueue queue {};
        localContsLock->Lock();
        localContinuations->Exchange(queue);
        localContsLock->Unlock();

        size_t count = 0;
        while (!queue.Empty())
        {
            auto* iop = queue.PopBack();
            count++;

            iop->Continuation(iop, iop->continuationOpaque);
        }

        Log("Ran %zu IOP continuations", LogLevel::Verbose, count);
    }
}
