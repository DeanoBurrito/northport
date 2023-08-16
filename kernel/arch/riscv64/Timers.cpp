#include <arch/Timers.h>
#include <arch/riscv64/Sbi.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <ArchHints.h>
#include <UnitConverter.h>
#include <Time.h>

namespace Npk
{
    sl::ScaledTime timerPeriod;
    
    inline uint64_t RdTime()
    {
        uintptr_t value;
        asm volatile("rdtime %0" : "=r"(value) :: "memory");
        return value;
    }

    void InitTimers()
    {
        ASSERT(SbiExtensionAvail(SbiExt::Time), "SBI time extension not available.");

        using namespace Config;
        DtProp* freqProp = DeviceTree::Global().Find("/cpus")->FindProp("timebase-frequency");
        ASSERT(freqProp != nullptr, "No timebase-frequency node.");
        
        timerPeriod = sl::ScaledTime::FromFrequency(freqProp->ReadValue(1));
        sl::UnitConversion freqUnits = sl::ConvertUnits(freqProp->ReadValue(1), sl::UnitBase::Decimal);
        Log("Platform timer: freq=%lu.%lu%shz", LogLevel::Info, freqUnits.major, freqUnits.minor, freqUnits.prefix);
    }

    void (*timerCallback)();
    void SetSysTimer(size_t nanoseconds, void (*callback)())
    {
        const size_t triggerTime = RdTime() + (nanoseconds / timerPeriod.ToNanos());
        if (callback != nullptr)
            timerCallback = callback;
        
        SbiSetTimer(triggerTime);
    }

    size_t SysTimerMaxNanos()
    {
        if (timerPeriod.ToNanos() > 0)
            return (size_t)-1ul; //more nanoseconds than we can count.
        ASSERT_UNREACHABLE();
    }

    void PolledSleep(size_t nanoseconds)
    {
        const uint64_t target = RdTime() + (nanoseconds / timerPeriod.ToNanos());
        while (RdTime() < target)
            sl::HintSpinloop();
    }

    size_t PollTimer()
    {
        return RdTime();
    }

    size_t PolledTicksToNanos(size_t ticks)
    {
        return ticks * timerPeriod.ToNanos();
    }
    
    const char* SysTimerName()
    {
        return "sbi";
    }

    const char* PollTimerName()
    {
        return "rdtime";
    }
}
