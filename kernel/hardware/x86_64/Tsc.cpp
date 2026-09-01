#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/Hpet.hpp>
#include <hardware/x86_64/Pit.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <hardware/common/timer/AcpiTimer.hpp>
#include <Core.hpp>
#include <lib/Units.hpp>

namespace Npk
{
    enum class RefTimerType
    {
        None,
        AcpiPm,
        Hpet,
        Pit,
    };

    enum class FreqSource
    {
        None,
        UserConfig,
        PvClock,
        Cpuid15,
        HypervisorLeaf,
        Measured,
        Cpuid16,
    };

    struct RefTimerControl
    {
        RefTimerType which;
        uint64_t frequency;
        uint64_t counterMask;
    };

    struct RefSample
    {
        uint64_t tsc;
        uint64_t ref;
        uint64_t accuracy;
    };

    struct TscSample
    {
        uint64_t frequency;
        uint64_t errorPpm;
        bool valid;
    };

    static sl::SpinLock pitLock;
    static RefTimerControl refTimer;
    static uint64_t tscFreq;
    static FreqSource tscFreqSource;
    static RefSample tscBootSample;

    static uint64_t CalcDeltaPpm(uint64_t a, uint64_t b)
    {
        const auto diff = a > b ? a - b : b - a;
        const auto base = a > b ? b : a;

        if (base == 0)
            return ~0ull;

        return diff * 1'000'000 / base;
    }

    void InitRefTimers(uintptr_t& virtBase)
    {
        if (InitAcpiTimer(virtBase))
        {
            refTimer.which = RefTimerType::AcpiPm;
            refTimer.frequency = AcpiTimerFrequency();
            refTimer.counterMask = 0xFF'FFFFull;
            if (AcpiTimerIs32Bit())
                refTimer.counterMask |= 0xFFull << 24;
        }
        else if (InitHpet(virtBase))
        {
            refTimer.which = RefTimerType::Hpet;
            refTimer.frequency = HpetFrequency();
            refTimer.counterMask = 0xFFFF'FFFFull;
            if (HpetIs64Bit())
                refTimer.counterMask = ~0ull;
        }
        else
        {
            NPK_ASSERT(!ReadConfigUint("npk.x86.ignore_pit", false));

            refTimer.which = RefTimerType::Pit;
            refTimer.frequency = PitFrequency;
            refTimer.counterMask = 0xFFFF;
        }

        const char* timerName = [](RefTimerType t) -> auto
        {
            switch (t)
            {
            case RefTimerType::AcpiPm:
                return "ACPI PM";
            case RefTimerType::Hpet:
                return "HPET";
            case RefTimerType::Pit:
                return "PIT";
            default:
                return "unknown";
            }
        }(refTimer.which);

        const auto conv = sl::ConvertUnits(refTimer.frequency, 
            sl::UnitBase::Decimal);
        Log("Reference timer: %s, %zuHz (%zu.%zu %sHz), %zu-bit counter",
            LogLevel::Info, timerName, refTimer.frequency, conv.major,
            conv.minor, conv.prefix, sl::PopCount(refTimer.counterMask));
    }

    static uint64_t ReadRefTimer()
    {
        switch (refTimer.which)
        {
        case RefTimerType::AcpiPm:
            return AcpiTimerRead();

        case RefTimerType::Hpet:
            return HpetRead();

        case RefTimerType::Pit:
            return ReadPit();

        default:
            NPK_UNREACHABLE();
        }
    }

    static RefSample TakeSample()
    {
        const uint64_t t0 = ReadTsc();
        const uint64_t ref = ReadRefTimer();
        const uint64_t t1 = ReadTsc();

        RefSample sample {};
        sample.tsc = t0 + ((t1 - t0) / 2);
        sample.ref = ref;
        sample.accuracy = t1 - t0;

        return sample;
    }

    static TscSample DoMeasureTsc(uint64_t sampleNs)
    {
        NPK_CHECK(refTimer.which != RefTimerType::None, {});

        auto ticks = sampleNs * refTimer.frequency / sl::Nanos;
        ticks = sl::Clamp(ticks, (uint64_t)1, refTimer.counterMask / 4);

        if (refTimer.which == RefTimerType::Pit)
        {
            pitLock.Lock();
            StartPit();
        }

        const auto begin = TakeSample();
        auto end = begin;
        while (((end.ref - begin.ref) & refTimer.counterMask) < ticks)
            end = TakeSample();

        if (refTimer.which == RefTimerType::Pit)
            pitLock.Unlock();

        const uint64_t refDelta = (end.ref - begin.ref) & refTimer.counterMask;
        const uint64_t tscDelta = end.tsc - begin.tsc;
        NPK_CHECK(refDelta != 0 && tscDelta != 0, {});

        const uint64_t freq = tscDelta * refTimer.frequency / refDelta;
        const uint64_t bracketPpm = ((begin.accuracy - end.accuracy) / 2) 
            * 1'000'000 / tscDelta;
        const uint64_t refPpm = 1'000'000 / refDelta;

        TscSample result {};
        result.frequency = freq;
        result.errorPpm = bracketPpm + refPpm;
        result.valid = true;

        const auto conv = sl::ConvertUnits(freq, sl::UnitBase::Decimal);
        Log("TSC measurement: freq=%zu (%zu.%zu %sHz), +/- %zuppm",
            LogLevel::Verbose, result.frequency, conv.major, conv.minor,
            conv.prefix, result.errorPpm);

        return result;
    }

    static TscSample MeasureTsc(uint64_t sampleNs, uint64_t targetPpm,
        size_t maxRuns)
    {
        TscSample best {};

        for (size_t i = 0; i < maxRuns; i++)
        {
            const auto run = DoMeasureTsc(sampleNs);
            if (!run.valid)
                continue;

            if (!best.valid || run.errorPpm < best.errorPpm)
                best = run;
            if (best.errorPpm <= targetPpm)
                break;
        }

        return best;
    }

    static uint64_t CalibrateTsc(FreqSource& outSrc)
    {
        const char* source = "unknown";
        uint64_t freq = 0;

        const auto override = ReadConfigUint("npk.x86.tsc_freq", 0);
        if (override != 0)
        {
            source = "config override";
            freq = override;
            outSrc = FreqSource::UserConfig;
        }

        auto pvFreq = PvClockTscFrequency();
        if (pvFreq.HasValue())
        {
            source = "pv clock";
            freq = *pvFreq;
            outSrc = FreqSource::PvClock;
        }

        CpuidLeaf leaf {};
        const size_t maxBaseLeaf = DoCpuid(0, 0, leaf).a;
        const size_t maxHypervisorLeaf = DoCpuid(HypervisorLeaf, 0, leaf).a;
        if (freq == 0 && maxBaseLeaf >= 0x15)
        {
            DoCpuid(0x15, 0, leaf);
            
            if (leaf.a != 0 && leaf.b != 0 && leaf.c != 0)
            {
                source = "cpuid 0x15";
                freq = ((uint64_t)leaf.c * (uint64_t)leaf.b) / (uint64_t)leaf.a;
                outSrc = FreqSource::Cpuid15;
            }
        }

        if (freq == 0 && maxHypervisorLeaf >= HypervisorLeaf + 0x10)
        {
            DoCpuid(HypervisorLeaf + 0x10, 0, leaf);
            
            if (leaf.a != 0)
            {
                source = "cpuid 0x4000'0010";
                freq = (uint64_t)leaf.a * 1000;
                outSrc = FreqSource::HypervisorLeaf;
            }
        }

        const uint64_t calibNs = ReadConfigUint("npk.x86.tsc_calib_ns", 
            50'000'000); //50ms
        const uint64_t targetPpm = ReadConfigUint("npk.x86.tsc_target_ppm", 20);
        const size_t maxRuns = ReadConfigUint("npk.x86.tsc_calib_runs", 10);

        //we measure regardless of the above attempts, so we can do a sanity
        //check.
        const auto measured = MeasureTsc(calibNs, targetPpm, maxRuns);
        if (freq == 0 && measured.valid)
        {
            freq = measured.frequency;
            source = "measurement";
            outSrc = FreqSource::Measured;

            Log("TSC frequency measured with +/- %zuppm", LogLevel::Info,
                measured.errorPpm);

            //since tsc frequency is measured, store a snapshot of the
            //reference timer for refining the tsc freq later on.
            //NOTE: the PIT is not allowed for this use as its counter is
            //very smol.
            if (refTimer.which != RefTimerType::Pit)
                tscBootSample = TakeSample();
            else
                tscBootSample = {};
        }
        
        if (freq == 0 && maxBaseLeaf >= 0x16)
        {
            DoCpuid(0x16, 0, leaf);

            if (leaf.a != 0)
            {
                source = "cpuid 0x16";
                freq = (uint64_t)leaf.a * 1'000'000;
                outSrc = FreqSource::Cpuid16;
            }
        }

        const auto conv = sl::ConvertUnits(freq, sl::UnitBase::Decimal);
        Log("TSC frequency is %zuHz (%zu.%zu %sHz) according to %s",
            LogLevel::Info, freq, conv.major, conv.minor, conv.prefix, source);

        //do a sanity check, catches bad hypervisors.
        if (measured.valid)
        {
            const auto delta = CalcDeltaPpm(freq, measured.frequency);

            if (delta > 1000)
            {
                Log("TSC frequency from %s varies from measurements: %zu vs %zu"
                    ", +/- %zuppm", LogLevel::Warning, source, freq,
                    measured.frequency, delta);
            }
        }

        return freq;
    }

    void InitTsc()
    {
        if (MyCoreId() == 0)
        {
            tscFreq = CalibrateTsc(tscFreqSource);
            NPK_ASSERT(tscFreq != 0 && tscFreqSource != FreqSource::None);
            return;
        }

        if (!ReadConfigUint("npk.x86.tsc_verify", true))
            return;

        const auto sampleNs = ReadConfigUint("npk.x86.tsc_verify_ns",
            2'000'000);
        const auto measured = DoMeasureTsc(sampleNs);
        NPK_CHECK(measured.valid, );

        const auto delta = CalcDeltaPpm(tscFreq, measured.frequency);
        NPK_ASSERT(delta + measured.errorPpm < 10'000);

        auto conv = sl::ConvertUnits(measured.frequency, sl::UnitBase::Decimal);
        Log("TSC frequency verified as %zuHz (%zu.%zu %sHz)", LogLevel::Verbose,
            measured.frequency, conv.major, conv.minor, conv.prefix);
    }

    uint64_t TscFrequency()
    {
        return tscFreq;
    }
}
