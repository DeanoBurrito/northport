#include <arch/riscv64/Timers.h>
#include <arch/riscv64/Sbi.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <UnitConverter.h>
#include <Time.h>

namespace Npk
{
    sl::ScaledTime timerPeriod;

    [[gnu::always_inline]]
    inline uint64_t RdTime()
    {
#ifdef NP_RV64_NO_RDTIME_INSTR
        return 0;
#else
        uintptr_t value;
        asm volatile("rdtime %0" : "=r"(value) :: "memory");
        return value;
#endif
    }
    
    void InitTimers()
    {
        ASSERT(SbiExtensionAvail(SbiExt::Time), "SBI time extension not available.");

        using namespace Config;
        auto freqProp = DeviceTree::Global().GetNode("/cpus")->GetProp("timebase-frequency");
        ASSERT(freqProp.HasValue(), "No timebase-frequency node.");
        
        timerPeriod = sl::ScaledTime::FromFrequency(freqProp->ReadNumber());
        sl::UnitConversion freqUnits = sl::ConvertUnits(freqProp->ReadNumber(), sl::UnitBase::Decimal);
        Log("Platform timer: freq=%lu.%lu%shz", LogLevel::Info, freqUnits.major, freqUnits.minor, freqUnits.prefix);
    }

    void (*timerCallback)(size_t);
    void SetTimer(size_t nanoseconds, void (*callback)(size_t))
    {
        const size_t triggerTime = RdTime() + (nanoseconds / timerPeriod.ToScale(sl::TimeScale::Nanos).units);
        if (callback != nullptr)
            timerCallback = callback;
        
        SbiSetTimer(triggerTime);
    }

    const char* ActiveTimerName()
    {
        return "sbi";
    }
}
