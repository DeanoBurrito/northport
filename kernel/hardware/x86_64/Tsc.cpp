#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Hpet.hpp>
#include <hardware/x86_64/Pit.hpp>
#include <hardware/common/AcpiTimer.hpp>
#include <CoreApi.hpp>
#include <Maths.h>
#include <UnitConverter.h>

namespace Npk
{
    void InitReferenceTimers(uintptr_t& virtBase)
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

    uint64_t ReferenceSleep(uint64_t nanos)
    {
        //TODO: maybe we should abstract this behind a `struct calib_timer_source`
        //or something, this is a bit silly. :wa
        if (AcpiTimerAvailable())
        {
            const uint64_t sleepTicks = nanos * AcpiTimerFrequency() / sl::Nanos;

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

    sl::Opt<uint64_t> CoalesceTimerData(sl::Span<uint64_t> runs, size_t allowedOutliers)
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


    CPU_LOCAL(uint64_t, tscFreq);

    static uint64_t Calibrate()
    {
        /* Calibrating the tsc, or: "Why let things that should be simple, be simple".
         * We try a number of ways to calibrate the tsc, moving on to the next if we fail:
         * 1. read the values from cpuid leaf 0x15
         * 2. read the values from cpuid leaf 0x16
         * 3. read the values from cpuid leaf 0x4000'0010
         * 4. calibrate against another timer:
         *  4a. acpi pm timer
         *  4b. hpet
         *  4c. pit
         * There is also the option of the user explicitly telling us the tsc freq, if they want.
         */

        if (auto freq = ReadConfigUint("npk.x86.tsc_freq_override", 0); freq != 0)
        {
            Log("TSC frequency set to %zuHz by command line override.", LogLevel::Trace,
                freq);
            return freq;
        }

        CpuidLeaf cpuid {};
        const size_t baseLeaves = DoCpuid(BaseLeaf, 0, cpuid).a;

        //1.
        DoCpuid(0x15, 0, cpuid);
        if (baseLeaves >= 0x15 && cpuid.b != 0 && cpuid.a != 0)
        {
            const uint64_t freq = (cpuid.c * cpuid.b) / cpuid.a;
            Log("TSC frequency acquired from cpuid 0x15: %u / %u * %u = %luHz", LogLevel::Trace,
                cpuid.c, cpuid.b, cpuid.a, freq);
            return freq;
        }

        //2.
        DoCpuid(0x16, 0, cpuid);
        if (baseLeaves >= 0x16 && cpuid.a != 0)
        {
            Log("TSC frequency acquired from cpuid 0x15: %uMHz", LogLevel::Trace, cpuid.a);
            return cpuid.a * 1'000'000;
        }

        //3.
        DoCpuid(HypervisorLeaf, 0, cpuid);
        if (cpuid.a >= 0x10 && DoCpuid(HypervisorLeaf + 0x10, 0, cpuid).a != 0)
        {
            Log("TSC frequency acquired from cpuid 0x4000'0010: %uKHz", LogLevel::Trace, cpuid.a);
            return cpuid.a * 1000;
        }

        //4.
        constexpr size_t MaxCalibRuns = 64;
        const size_t calibRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_calibration_runs", 10), 1, MaxCalibRuns);
        const size_t sampleFreq = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_sample_freq", 100), 10, 1000);
        const size_t neededRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_needed_runs", 7), 1, calibRuns);
        const size_t controlRuns = sl::Clamp<size_t>(ReadConfigUint("npk.x86.tsc_control_runs", 5), 1, calibRuns);
        const bool dumpCalibData = ReadConfigUint("npk.x86.tsc_dump_calibration", true);

        size_t controlOffset = 0;
        uint64_t calibData[MaxCalibRuns];
        uint64_t calibNanos = sl::TimeCount(sampleFreq, 1).Rebase(sl::Nanos).ticks;
        Log("Calibrating TSC: sampling=%zu hz, runs=%zu (mulligans=%zu, control=%zu)", LogLevel::Trace,
            sampleFreq, calibRuns, calibRuns - neededRuns, controlRuns);

        //control runs, determine time taken to read reference timer
        for (size_t i = 0; i < controlRuns; i++)
        {
            const size_t tscBegin = ReadTsc();
            ReferenceSleep(0);
            const size_t tscEnd = ReadTsc();

            controlOffset += tscEnd - tscBegin;
        }

        controlOffset /= controlRuns;
        Log("Control offset for reference timer: %zu tsc ticks", LogLevel::Trace, controlOffset);

        for (size_t i = 0; i < calibRuns; i++)
        {
            const uint64_t tscBegin = ReadTsc();
            const uint64_t realCalibNanos = ReferenceSleep(calibNanos);
            const uint64_t tscEnd = ReadTsc();

            if (realCalibNanos == 0)
            {
                //something went wrong with the calibration sleep (massive SMI?)
                calibData[i] = 0;
                continue;
            }

            calibData[i] = (tscEnd - tscBegin) - controlOffset;
            calibData[i] = (calibData[i] * calibNanos) / realCalibNanos; //oversleep correction
            if (dumpCalibData)
            {
                Log("TSC calibratun run: begin=%zu, end=%zu, adjusted=%zu, slept=%zuns", LogLevel::Verbose,
                    tscBegin, tscEnd, calibData[i], realCalibNanos);
            }
        }

        const auto maybeTscPeriod = CoalesceTimerData({ calibData, calibRuns }, calibRuns - neededRuns);
        NPK_ASSERT(maybeTscPeriod.HasValue());

        const uint64_t tscFreq = *maybeTscPeriod * sampleFreq;
        const auto conv = sl::ConvertUnits(tscFreq, sl::UnitBase::Decimal);
        Log("TSC calibrated as %zu Hz (%zu.%zu %sHz)", LogLevel::Info, tscFreq,
            conv.major, conv.minor, conv.prefix);
        return tscFreq;
    }

    bool CalibrateTsc()
    {
        if (!CpuHasFeature(CpuFeature::Tsc))
            return false;
        if (!CpuHasFeature(CpuFeature::InvariantTsc) && MyCoreId() == 0)
            Log("This cpu does not report an invariant tsc.", LogLevel::Warning);

        //TODO: if we have invariant tsc, can we clone BSP's calibration data?
        const uint64_t freq = Calibrate();
        *tscFreq = freq;

        return freq != 0;
    }

    uint64_t MyTscFrequency()
    {
        return *tscFreq;
    }
}
