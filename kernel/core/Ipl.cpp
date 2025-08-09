#include <CoreApi.hpp>
#include <CoreApiPrivate.hpp>

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

        while (true)
        {
            dpcQueueLock->Lock();
            Dpc* dpc = dpcQueue->PopFront();
            dpcQueueLock->Unlock();

            if (dpc == nullptr)
                break;
            dpc->function(dpc, dpc->arg);
        }
    }

    void LowerIpl(Ipl target)
    {
        while (true)
        {
            const bool restoreIntrs = IntrsOff();
            const Ipl currentIpl = *localIpl;
            if (restoreIntrs)
                IntrsOn();

            if (currentIpl == target)
                break; //reached the target level, we're done

            //complete any work pending for this level and then lower ipl by 1
            switch (currentIpl)
            {
            case Ipl::Interrupt: break; //no-op
            case Ipl::Dpc:
                RunDpcs();
                break;
            case Ipl::Passive: 
                //there may be a pending context switch, let the scheduler
                //know it can perform that now if it wants.
                Private::OnPassiveRunLevel();
                break;
            }

            const Ipl nextIpl = static_cast<Ipl>(static_cast<unsigned>(currentIpl) - 1);
            localIpl = nextIpl;

            if (restoreIntrs)
                IntrsOn();
        }
    }

    sl::StringSpan IplStr(Ipl which)
    {
        constexpr sl::StringSpan iplStrs[] =
        {
            "Passive",
            "Dpc",
            "Interrupt"
        };

        if (static_cast<size_t>(which) > static_cast<size_t>(Ipl::Interrupt))
            return "unknown ipl";
        return iplStrs[static_cast<size_t>(which)];
    }
    static_assert(static_cast<Ipl>(0) == Ipl::Passive);
    static_assert(static_cast<Ipl>(1) == Ipl::Dpc);
    static_assert(static_cast<Ipl>(2) == Ipl::Interrupt);

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
