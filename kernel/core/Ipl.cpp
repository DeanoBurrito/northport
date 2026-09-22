#include <private/Core.hpp>

namespace Npk
{
    CPU_LOCAL(IntrSpinLock, dpcQueueLock);
    CPU_LOCAL(DpcQueue, dpcQueue);

    void AssertIpl(Ipl target)
    {
        NPK_ASSERT(target == (Ipl)(HwGetIplWord() & IplWordLevelMask));
    }

    Ipl CurrentIpl()
    {
        return (Ipl)(HwGetIplWord() & IplWordLevelMask);
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

    void Private::ClearIplPending(IplWord bits)
    {
        auto word = HwGetIplWord();

        while (!HwCompareExchangeIplWord(word, word & ~bits))
        {}
    }

    static IplWord StagePendingMask(Ipl level)
    {
        switch (level)
        {
        case Ipl::Alarm:
            return IplWordAlarmBit;

        case Ipl::Dpc:
            return IplWordDpcBit | IplWordWaitableBit;

        default:
            return 0;
        }
    }

    void Private::LowerIplSlowPath(Ipl target)
    {
        NPK_ASSERT(target < CurrentIpl());

        while (true)
        {
            const auto word = HwGetIplWord();
            const auto current = static_cast<Ipl>(word & IplWordLevelMask);
            if (current == target)
                break;

            const auto pendingMask = StagePendingMask(current);
            if ((word & pendingMask) != 0)
            {
                const bool prevIntrs = IntrsOn();

                switch (current)
                {
                case Ipl::Alarm:
                    ClearIplPending(IplWordAlarmBit);
                    OnAlarmIpl();
                    break;

                case Ipl::Dpc:
                    if ((word & IplWordDpcBit) != 0)
                    {
                        ClearIplPending(IplWordDpcBit);
                        RunDpcs();
                        break;
                    }

                    ClearIplPending(IplWordWaitableBit);
                    SignalPendingWaitables();
                    break;

                default:
                    NPK_UNREACHABLE();
                }

                if (!prevIntrs)
                    IntrsOff();
                continue;
            }

            if (current == Ipl::Tlb)
            {
                const bool prevIntrs = IntrsOn();
                TlbSyncQuiesce();
                if (!prevIntrs)
                    IntrsOff();
            }

            auto expected = word;
            const auto next = static_cast<IplWord>(current) - 1;
            while (!HwCompareExchangeIplWord(expected,
                (expected & IplWordWorkMask) | next))
            {
                NPK_ASSERT((expected & IplWordLevelMask)
                    == static_cast<IplWord>(current));

                if ((expected & pendingMask) != 0)
                    break;
            }
        }

        if (target != Ipl::Passive)
            return;

        if ((HwGetIplWord() & IplWordRcuBit) != 0)
        {
            ClearIplPending(IplWordRcuBit);
            CheckPendingRcuQuiesce();
        }

        const auto word = HwGetIplWord();
        if ((word & (IplWordSwitchBit | IplWordInSwitchBit)) == IplWordSwitchBit)
        {
            ClearIplPending(IplWordSwitchBit);
            Yield();
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

        HwSetPending(IplWordDpcBit);
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
