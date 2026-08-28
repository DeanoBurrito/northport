#include <private/Core.hpp>

namespace Npk
{
    struct CycleAccounting
    {
        CycleAccountStats stats;
        sl::TimePoint periodBegin;
        CycleAccount current;
    };

    CPU_LOCAL(CycleAccounting, localCycles);
    static sl::Atomic<sl::TimePoint> systemTimeOffset {};

    void Private::ResetCycleAccounts(CycleAccount first)
    {
        const bool prevIntrs = IntrsOff();

        localCycles->stats.Reset();
        localCycles->current = first;
        localCycles->periodBegin = HwReadTimestamp();

        if (prevIntrs)
            IntrsOn();
    }

    CycleAccount SetCycleAccount(CycleAccount who)
    {
        const bool prevIntrs = IntrsOff();

        const auto now = HwReadTimestamp();
        const auto period = (now - localCycles->periodBegin).epoch;
        const auto prevAccount = localCycles->current;

        localCycles->stats.Add(prevAccount, period);

        //TODO: update per-thread user + kernel times

        localCycles->current = who;
        localCycles->periodBegin = now;

        if (prevIntrs)
            IntrsOn();

        return prevAccount;
    }

    NpkStatus GetCycleAccounting(CycleAccountStats& outStats, CpuId who)
    {
        if (who == MyCoreId())
        {
            if (localCycles->stats.Copy(outStats))
                return NpkStatus::Success;
            return NpkStatus::InternalError;
        }

        return NpkStatus::Unsupported;
    }

    sl::TimePoint GetTime()
    {
        const auto now = HwReadTimestamp();
        const auto wall = now + systemTimeOffset.Load(sl::Relaxed);

        return wall;
    }

    sl::TimePoint GetTimeOffset()
    {
        return systemTimeOffset.Load(sl::Relaxed);
    }

    void SetTimeOffset(sl::TimePoint offset)
    {
        auto prev = systemTimeOffset.Exchange(offset, sl::Relaxed);

        const auto dirStr = offset.epoch > prev.epoch ? "+" : "-";
        const auto diff = offset.epoch > prev.epoch ? offset.epoch - prev.epoch
            : prev.epoch - offset.epoch;
        auto date = sl::CalendarPoint::From(offset);

        Log("System time offset set: %s%zu, new base is %02u/%02u/%02" 
            PRIu32" %02u:%02u.%02u",
            LogLevel::Info, dirStr, diff, date.dayOfMonth, date.month, 
            date.year, date.hour, date.minute, date.second);
    }
}
