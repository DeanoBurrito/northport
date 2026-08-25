#include <private/Core.hpp>

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
                dpc->complete.Store(true, sl::Release);
            }
        }
    }

    void LowerIpl(Ipl target)
    {
        while (true)
        {
            const auto current = *localIpl;
            if (current == target)
                break;

            const bool prevIntrs = IntrsOff();
            bool moreWork = false;
            switch (current)
            {
            case Ipl::Interrupt:
                break;

            case Ipl::Tlb:
                IntrsOn();
                TlbSyncQuiesce();
                IntrsOff();
                break;

            case Ipl::Alarm:
                IntrsOn();
                Private::OnAlarmIpl();
                IntrsOff();
                moreWork = Private::AlarmIplHasPendingWork();
                break;

            case Ipl::Dpc:
                IntrsOn();
                RunDpcs();
                IntrsOff();
                dpcQueueLock->Lock();
                moreWork = !dpcQueue->Empty();
                dpcQueueLock->Unlock();

                if (!moreWork && target == Ipl::Passive)
                    Private::SignalPendingWaitables();
                break;

            case Ipl::Passive:
                break;
            }

            if (moreWork)
            {
                if (prevIntrs)
                    IntrsOn();
                continue;
            }

            localIpl = (Ipl)((unsigned)current - 1);
            if (prevIntrs)
                IntrsOn();
        }

        if (target == Ipl::Passive)
        {
            Private::CheckPendingRcuQuiesce();
            Private::CheckPendingContextSwitch();
        }
    }

    NpkStatus ResetDpc(Dpc* dpc, DpcEntry func, void* arg, bool force)
    {
        if (dpc == nullptr)
            return NpkStatus::InvalidArg;
        if (func == nullptr)
            return NpkStatus::InvalidArg;

        const bool complete = dpc->complete.Load(sl::Acquire);
        if (!complete && !force)
            return NpkStatus::Busy;

        dpc->complete.Store(false, sl::Release);
        dpc->arg = arg;
        dpc->function = func;

        return NpkStatus::Success;
    }

    void QueueDpc(Dpc* dpc)
    {
        NPK_CHECK(dpc != nullptr, );
        NPK_CHECK(dpc->function != nullptr, );

        dpc->complete.Store(false, sl::Release);

        if (CurrentIpl() < Ipl::Dpc)
        {
            const auto prevIpl = RaiseIpl(Ipl::Dpc);
            dpc->function(dpc, dpc->arg);
            dpc->complete.Store(true, sl::Release);
            LowerIpl(prevIpl);

            return;
        }

        dpcQueueLock->Lock();
        dpcQueue->PushBack(dpc);
        dpcQueueLock->Unlock();
    }

    void SpinUntilDpcCompleted(Dpc* dpc)
    {
        AssertIpl(Ipl::Passive);
        if (dpc == nullptr)
            return;

        while (!dpc->complete.Load(sl::Relaxed))
            sl::HintSpinloop();
    }
}
