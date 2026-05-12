#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/RefTimers.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <Core.hpp>
#include <lib/Maths.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    constexpr uint64_t TscFreqUndetermined = 0;
    constexpr uint64_t TscFreqPending = 1;

    /* Protocol: a value of 0 means tsc frequency is undetermined for this
     * group of cores, a value of 1 means a cpu is currently determining the
     * frequency, all other values are the tsc frequency.
     * The first cpu to request the frequency will see it's value 0, so it
     * knows it should set it to 1 and begin calibration. Other cpus reading a
     * value of 1 will spin until the value changes.
     */
    sl::Atomic<uint64_t> tscFrequency = TscFreqUndetermined;
    //TODO: this should be per-socket, or more easily per-node

    static void DoTscCalibration()
    {
        if (auto freq = ReadConfigUint("npk.x86.tsc_freq_override", 0); freq)
        {
            Log("TSC frequency set to %zuHZ by command line", LogLevel::Info,
                freq);

            tscFrequency.Store(freq, sl::Release);
            return;
        }

        CpuidLeaf cpuid {};
        const size_t baseLeaves = DoCpuid(BaseLeaf, 0, cpuid).a;

        DoCpuid(0x15, 0, cpuid);
        if (baseLeaves >= 0x15 && cpuid.b != 0 && cpuid.a != 0)
        {
            const uint64_t freq = (cpuid.c * cpuid.b) / cpuid.a;
            Log("TSC frequency from cpuid 0x15 set to %luHz", LogLevel::Info,
                freq);

            tscFrequency.Store(freq, sl::Release);
            return;
        }

        DoCpuid(HypervisorLeaf, 0, cpuid);
        if (cpuid.a >= 0x10 && DoCpuid(HypervisorLeaf + 0x10, 0, cpuid).a != 0)
        {
            const uint64_t freq = cpuid.a * 1000;
            Log("TSC frequency from cpuid 0x4000'0010: %luHz", LogLevel::Trace,
                freq);

            tscFrequency.Store(freq, sl::Release);
            return;
        }

        DoCpuid(0x16, 0, cpuid);
        if (baseLeaves >= 0x16 && cpuid.a != 0)
        {
            const uint64_t freq = cpuid.a * 1'000'000;
            Log("TSC frequency from cpuid 0x15: %luHz", LogLevel::Info,
                freq);

            tscFrequency.Store(freq, sl::Release);
            return;
        }

        constexpr size_t MaxRuns = 64;
        uint64_t data[MaxRuns];

        const auto runs = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.tsc_calibration_runs", 10), 1, MaxRuns);
        const auto sampleFreq = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.tsc_sample_freq", 100), 10, 1000);
        const auto neededRuns = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.tsc_needed_runs", 7), 1, runs);
        const auto controlRuns = sl::Clamp<size_t>(
            ReadConfigUint("npk.x86.tsc_control_runs", 5), 1, MaxRuns);
        const auto dumpResults = 
            ReadConfigUint("npk.x86.tsc_show_calibration", true);

        size_t controlOffset = 0;
        const uint64_t calibNanos = 
            sl::TimeCount(sampleFreq, 1).Rebase(sl::Nanos).ticks;

        Log("Calibrating TSC: sampling=%zuHz, runs=%zu (%zu needed)",
            LogLevel::Verbose, sampleFreq, runs, neededRuns);

        //control runs, determine time taken to read reference timer
        for (size_t i = 0; i < controlRuns; i++)
        {
            AcquireRefTimersLock();
            const auto tscBegin = ReadTsc();
            RefTimersSleep(0);
            const auto tscEnd = ReadTsc();
            ReleaseRefTimersLock();

            controlOffset += tscEnd - tscBegin;
        }
        controlOffset /= controlRuns;

        if (controlRuns != 0)
        {
            Log("Control runs: %zu, average tsc ticks taken = %zu",
                LogLevel::Verbose, controlRuns, controlOffset);
        }

        for (size_t i = 0; i < runs; i++)
        {
            AcquireRefTimersLock();
            const auto tscBegin = ReadTsc();
            const auto sleptNanos = RefTimersSleep(calibNanos);
            const auto tscEnd = ReadTsc();
            ReleaseRefTimersLock();

            if (sleptNanos == 0)
            {
                //reference timer wasn't happy with something.
                data[i] = 0;
                continue;
            }

            data[i] = (tscEnd - tscBegin);
            data[i] = data[i] - controlOffset;
            data[i] = (data[i] * calibNanos) / sleptNanos;

            if (dumpResults)
            {
                Log("Calibration run %zu: begin=%zu, end=%zu, adjusted=%zu, "
                    "slept=%zu", LogLevel::Trace, i, tscBegin, tscEnd, data[i],
                    sleptNanos);
            }
        }

        const auto period = CoalesceTimerData({ data, runs }, runs -neededRuns);
        NPK_ASSERT(period.HasValue());

        const auto freq = *period * sampleFreq;
        const auto conv = sl::ConvertUnits(freq, sl::UnitBase::Decimal);
        Log("TSC calibrated as %zuHz (%zu.%zu %sHz)", LogLevel::Info, freq,
            conv.major, conv.minor, conv.prefix);

        tscFrequency.Store(freq, sl::Release);
    }

    void CalibrateTsc()
    {
        uint64_t freq = tscFrequency.Load(sl::Acquire);
        if (freq == TscFreqUndetermined)
        {
            if (tscFrequency.CompareExchange(freq, TscFreqPending, sl::AcqRel))
                DoTscCalibration();
        }

        while (freq == TscFreqPending)
        {
            sl::HintSpinloop();
            freq = tscFrequency.Load(sl::Acquire);
        }

        const auto conv = sl::ConvertUnits(freq, sl::UnitBase::Decimal);
        Log("Local TSC frequency is %zuHz (%zu.%zu %sHz)", LogLevel::Verbose, 
            freq, conv.major, conv.minor, conv.prefix);
    }

    uint64_t MyTscFrequency()
    {
        uint64_t freq = tscFrequency.Load(sl::Acquire);
        if (freq <= TscFreqPending)
            return 0;

        return freq;
    }
}
