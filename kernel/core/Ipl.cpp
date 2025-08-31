#include <CorePrivate.hpp>

namespace Npk
{
    CPU_LOCAL(IntrSpinLock, dpcQueueLock);
    CPU_LOCAL(DpcQueue, dpcQueue);
    CPU_LOCAL(Ipl, localIpl);

    void AssertIpl(Ipl target)
    {
        NPK_ASSERT(target == *localIpl);
    }

    Ipl CurrentIpl()
    {
        return *localIpl;
    }

    Ipl RaiseIpl(Ipl target)
    {
        const bool prevIntrs = IntrsOff();

        const Ipl prev = *localIpl;
        localIpl = target;
        NPK_ASSERT(target > prev);

        if (prevIntrs)
            IntrsOn();
        return prev;
    }

    static void RunDpcs()
    {
        NPK_ASSERT(CurrentIpl() == Ipl::Dpc);

        DpcQueue localQueue {};

        while (true)
        {
            dpcQueueLock->Lock();
            dpcQueue->Exchange(localQueue);
            dpcQueueLock->Unlock();

            if (localQueue.Empty())
                break;

            while (!localQueue.Empty())
            {
                auto dpc = localQueue.PopFront();
                dpc->function(dpc, dpc->arg);
            }
        }
    }

    void LowerIpl(Ipl target)
    {
        while (true)
        {
            const Ipl currentIpl = *localIpl;
            if (currentIpl == target)
                break;

            //finish any pending work for this IPL, then decrement it to
            //the next IPL.
            const bool restoreIntrs = IntrsOff();

            switch (currentIpl)
            {
            case Ipl::Interrupt:
                break;
            case Ipl::Dpc:
                RunDpcs();
                break;
            case Ipl::Passive:
                break;
            }

            localIpl = (Ipl)((unsigned)currentIpl - 1);

            if (restoreIntrs)
                IntrsOn();
        }

        if (target == Ipl::Passive)
            Private::OnPassiveRunLevel();
    }

    void QueueDpc(Dpc* dpc)
    {
        NPK_CHECK(dpc != nullptr, );
        NPK_CHECK(dpc->function != nullptr, );

        if (CurrentIpl() < Ipl::Dpc)
        {
            const auto prevIpl = RaiseIpl(Ipl::Dpc);
            dpc->function(dpc, dpc->arg);
            LowerIpl(prevIpl);
            return;
        }

        dpcQueueLock->Lock();
        dpcQueue->PushBack(dpc);
        dpcQueueLock->Unlock();
    }
}
