#include <KernelApi.hpp>

namespace Npk
{
    CPU_LOCAL(DpcQueue, dpcQueue);
    CPU_LOCAL(Ipl, localIpl);

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
            case Ipl::Clock: break; //no-op
            case Ipl::Dpc:
                {
                    Dpc* dpc = nullptr;
                    while ((dpc = dpcQueue->PopFront()) != nullptr)
                    {
                        if (restoreIntrs)
                            IntrsOn();
                        dpc->function(dpc, dpc->arg);
                        IntrsOff();
                    }
                    break;
                }
            case Ipl::Passive: break; //TODO: inform scheduler switch is possible
            }

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
            "Clock",
            "Interrupt"
        };

        if (static_cast<size_t>(which) > static_cast<size_t>(Ipl::Interrupt))
            return "unknown ipl";
        return iplStrs[static_cast<size_t>(which)];
    }
    static_assert(static_cast<Ipl>(0) == Ipl::Passive);
    static_assert(static_cast<Ipl>(1) == Ipl::Dpc);
    static_assert(static_cast<Ipl>(2) == Ipl::Clock);
    static_assert(static_cast<Ipl>(3) == Ipl::Interrupt);

    void QueueDpc(Dpc* dpc)
    {
        const Ipl prevIpl = RaiseIpl(Ipl::Dpc);
        dpcQueue->PushBack(dpc);
        LowerIpl(prevIpl);
    }
}
