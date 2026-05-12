#include <hardware/x86_64/RefTimers.hpp>
#include <hardware/x86_64/Hpet.hpp>
#include <hardware/x86_64/Pit.hpp>
#include <hardware/common/timer/AcpiTimer.hpp>
#include <Core.hpp>
#include <lib/Locks.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    void InitRefTimers(uintptr_t& virtBase)
    {
        if (InitAcpiTimer(virtBase))
        {
            Log("ACPI timer selected as reference timer.", LogLevel::Trace);
            return;
        }
        if (InitHpet(virtBase))
        {
            Log("HPET selected as reference timer.", LogLevel::Trace);
            return;
        }

        NPK_ASSERT(!ReadConfigUint("npk.x86.ignore_pit", false));
        //dont laugh at this lol: but I want to provide the option to ignore
        //the PIT, but if we reach this point we have no further calibration
        //sources and must abort.
        Log("PIT selected as reference timer.", LogLevel::Trace);
    }

    static sl::SpinLock pitLock {};

    void AcquireRefTimersLock()
    {
        if (AcpiTimerAvailable() || HpetAvailable())
            return;

        pitLock.Lock();
    }

    void ReleaseRefTimersLock()
    {
        if (AcpiTimerAvailable() || HpetAvailable())
            return;

        pitLock.Unlock();
    }

    uint64_t RefTimersSleep(uint64_t nanos)
    {
        if (AcpiTimerAvailable())
        {
            const uint64_t sleepTicks = nanos * AcpiTimerFrequency() /sl::Nanos;

            const uint64_t beginSleep = AcpiTimerRead();
            uint64_t endSleep = beginSleep;

            while (endSleep < beginSleep + sleepTicks)
                endSleep = AcpiTimerRead();

            const uint64_t sleptTicks = endSleep - beginSleep;
            return sleptTicks * (sl::Nanos / AcpiTimerFrequency());
        }
        else if (HpetAvailable())
        {
            const uint64_t sleepTicks = nanos * HpetFrequency() / sl::Nanos;

            const uint64_t beginSleep = HpetRead();
            uint64_t endSleep = beginSleep;

            while (endSleep < beginSleep + sleepTicks)
                endSleep = HpetRead();

            const uint64_t sleptTicks = endSleep - beginSleep;
            return sleptTicks * (sl::Nanos / HpetFrequency());
        }
        else
        {
            const uint64_t sleepTicks = nanos * PitFrequency / sl::Nanos;

            StartPit();
            const uint64_t beginSleep = ReadPit();
            uint64_t endSleep = beginSleep;

            while (endSleep < beginSleep + sleepTicks)
                endSleep = ReadPit();

            const uint64_t sleptTicks = endSleep - beginSleep;
            return sleptTicks * (sl::Nanos / PitFrequency);
        }
    }

    sl::Opt<uint64_t> CoalesceTimerData(sl::Span<uint64_t> runs, 
        size_t allowedOutliers)
    {
        uint64_t mean = 0;
        for (size_t i = 0; i < runs.Size(); i++)
            mean += runs[i];
        mean /= runs.Size();

        const uint64_t deviation = sl::StandardDeviation(runs);

        size_t validCount = 0;
        uint64_t accumulator = 0;
        for (size_t i = 0; i < runs.Size(); i++)
        {
            if (runs[i] < mean - deviation || runs[i] > mean + deviation)
                continue;

            validCount++;
            accumulator += runs[i];
        }

        if (validCount < runs.Size() - allowedOutliers)
            return {};

        accumulator /= validCount;
        return accumulator;
    }

}
