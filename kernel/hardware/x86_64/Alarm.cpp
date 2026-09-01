#include <private/Hardware.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <Core.hpp>

namespace Npk
{
    enum class AlarmSource : uint8_t
    {
        Tsc,
        Lapic,
    };

    enum class TimelineSource : uint8_t
    {
        None,
        Tsc,
        PvClock,
    };

    struct AlarmState
    {
        sl::TimeConversion toNanos;
        sl::TimeConversion toTicks;
        sl::TimeConversion tscToLapic;
        uint64_t refTsc;
        uint64_t refNanos;
        uint32_t lastPvVersion;
        TimelineSource timeSource;
        AlarmSource alarmSource;
        bool hasArmRequest;
        sl::TimePoint armedExpiry;
        uint64_t tscExpiry;
    };

    CPU_LOCAL(AlarmState, alarmState);

    SL_ALWAYS_INLINE
    static sl::TimePoint PlaceOnTimeline(AlarmState& state, uint64_t tscValue)
    {
        tscValue -= state.refTsc;
        tscValue = state.toNanos.Apply(tscValue);

        sl::TimePoint ret {};
        ret.epoch = state.refNanos;
        ret.epoch += tscValue;

        return ret;
    }

    static uint32_t LapicTicksUntil(AlarmState& state, uint64_t tscExpiry)
    {
        const auto now = ReadTsc();

        uint64_t accum = tscExpiry - now;
        if (tscExpiry <= now)
            accum = 0;

        accum = state.tscToLapic.Apply(accum);
        accum = sl::Clamp<decltype(accum)>(accum, 1, 0xFFFF'FFFF);

        return accum;
    }

    static void SetAlarm(AlarmState& state, sl::TimePoint expiry)
    {
        uint64_t sinceAnchor = expiry.epoch - state.refNanos;
        if (expiry.epoch <= state.refNanos)
            sinceAnchor = 0;
        sinceAnchor = state.toTicks.Apply(sinceAnchor);

        uint64_t deadline = sl::SaturatingAdd(state.refTsc, sinceAnchor);
        deadline = sl::Max<uint64_t>(1, deadline);

        if (state.alarmSource == AlarmSource::Tsc)
            return WriteMsr(Msr::TscDeadline, deadline);

        state.tscExpiry = deadline;
        auto lapicTicks = LapicTicksUntil(state, deadline);
        ArmLapicTimer(lapicTicks);
    }

    static void AdjustTimeline(AlarmState& state, sl::TimeConversion toNanos,
        sl::TimeConversion toTicks, uint64_t tscAnchor, uint64_t nanosAnchor)
    {
        NPK_ASSERT(toNanos.mul != 0 && toTicks.mul != 0);

        state.toNanos = toNanos;
        state.toTicks = toTicks;
        state.refTsc = tscAnchor;
        state.refNanos = nanosAnchor;
    }

    static void AdjustTimelineNow(AlarmState& state, sl::TimeConversion toNanos,
        sl::TimeConversion toTicks)
    {
        NPK_ASSERT(state.timeSource != TimelineSource::None);

        const auto tsc = ReadTsc();
        const auto nanos = PlaceOnTimeline(state, tsc).epoch;

        AdjustTimeline(state, toNanos, toTicks, tsc, nanos);

        if (state.hasArmRequest)
            SetAlarm(state, state.armedExpiry);
    }

    void HandleAlarmInterrupt()
    {
        auto& state = *alarmState;

        if (state.alarmSource == AlarmSource::Tsc)
            return DispatchAlarm();

        if (state.tscExpiry == 0)
            return;

        if (ReadTsc() >= state.tscExpiry)
            return DispatchAlarm();

        ArmLapicTimer(LapicTicksUntil(state, state.tscExpiry));
    }

    static uint32_t SyncPvTimeline(AlarmState& state)
    {
        while (true)
        {
            const uint32_t begin = PvClockVersion();

            if ((begin & 1) != 0)
            {
                sl::HintSpinloop();
                continue;
            }
            if (begin == state.lastPvVersion)
                return begin;

            auto pvStruct = ReadPvClock();
            if (PvClockVersion() != begin)
                return begin;

            const auto tpFreq = sl::TimePoint::Frequency;
            const auto pvFreq = PvClockFrequency(pvStruct);
            NPK_ASSERT(pvFreq != 0);
            NPK_ASSERT(ReadTsc() >= pvStruct.tscReference);

            const bool prevIntrs = IntrsOff();

            const bool init = state.timeSource == TimelineSource::None;
            auto before = PlaceOnTimeline(state, ReadTsc()).epoch;
            if (init)
                before = 0;

            const auto toNanos = sl::TimeConversion::Create(pvFreq, tpFreq);
            const auto toTicks = sl::TimeConversion::Create(tpFreq, pvFreq);
            AdjustTimeline(state, toNanos, toTicks, pvStruct.tscReference,
                pvStruct.systemTime);
            state.lastPvVersion = begin;

            if (!init)
                NPK_ASSERT(PlaceOnTimeline(state, ReadTsc()).epoch >= before);

            if (state.hasArmRequest)
                SetAlarm(state, state.armedExpiry);
            if (prevIntrs)
                IntrsOn();

            /*
            if (pvStruct.flags.Has(PvClockFlag::GuestStopped))
            {
                Log("Guest was stopped, a large backlog of events may fire",
                    LogLevel::Info);
            }
            */
        }
    }

    void InitLocalAlarm()
    {
        auto& state = *alarmState;
        NPK_ASSERT(state.timeSource == TimelineSource::None);

        if (!CpuHasFeature(CpuFeature::InvariantTsc))
        {
            if (!ReadConfigUint("npk.x86.allow_variant_tsc", false))
                Panic("Invariant TSC not detected", nullptr);

            Log("Invariant TSC not detected, continuing by config override.",
                LogLevel::Error);
        }

        if (!CpuHasFeature(CpuFeature::AlwaysRunningApic))
            Log("ARAT not detected", LogLevel::Warning);

        auto source = TimelineSource::Tsc;
        const char* timelineStr = "TSC";
        if (PvClockAvailable())
        {
            SyncPvTimeline(state);
            source = TimelineSource::PvClock;
            timelineStr = "PvClock";
        }
        else
        {
            const auto tscFreq = TscFrequency();
            NPK_ASSERT(tscFreq != 0);

            const auto tpFreq = sl::TimePoint::Frequency;
            const auto toNanos = sl::TimeConversion::Create(tscFreq, tpFreq);
            const auto toTicks = sl::TimeConversion::Create(tpFreq, tscFreq);

            AdjustTimeline(state, toNanos, toTicks, 0, 0);
        }

        //TODO: check coherence between all cpus

        const char* alarmStr;
        if (CpuHasFeature(CpuFeature::TscDeadline))
        {
            state.alarmSource = AlarmSource::Tsc;
            alarmStr = "TSC";

            WriteMsr(Msr::TscDeadline, 0);
        }
        else
        {
            state.alarmSource = AlarmSource::Lapic;
            state.tscToLapic = CalibrateLapicTimer();
            alarmStr = "LAPIC";
            
            NPK_ASSERT(state.tscToLapic.mul != 0);
        }

        SetupLapicTimer(state.alarmSource == AlarmSource::Tsc);
        DisarmLapicTimer();

        state.hasArmRequest = false;
        state.tscExpiry = 0;
        state.timeSource = source;

        Log("Local alarm ready: timeline=%s, alarm=%s.", LogLevel::Info,
            timelineStr, alarmStr);
    }

    void HwSetAlarm(sl::TimePoint expiry)
    {
        auto& state = *alarmState;
        NPK_ASSERT(state.timeSource != TimelineSource::None);

        const bool prevIntrs = IntrsOff();

        state.armedExpiry = expiry;
        state.hasArmRequest = true;
        if (state.timeSource == TimelineSource::PvClock)
            SyncPvTimeline(state);

        SetAlarm(state, expiry);

        if (prevIntrs)
            IntrsOn();
    }

    void HwClearAlarm()
    {
        auto& state = *alarmState;

        const bool prevIntrs = IntrsOff();

        state.hasArmRequest = false;
        if (state.alarmSource == AlarmSource::Tsc)
            WriteMsr(Msr::TscDeadline, 0);
        else
        {
            state.tscExpiry = 0;
            DisarmLapicTimer();
        }

        if (prevIntrs)
            IntrsOn();
    }

    sl::TimePoint HwReadTimestamp()
    {
        auto& state = *alarmState;

        if (state.timeSource == TimelineSource::None)
            return {};

        if (state.timeSource == TimelineSource::Tsc)
            return PlaceOnTimeline(state, ReadTsc());

        while (true)
        {
            const auto begin = SyncPvTimeline(state);
            const auto tsc = ReadTsc();
            if (PvClockVersion() != begin)
                continue;

            return PlaceOnTimeline(state, tsc);
        }
    }
}
