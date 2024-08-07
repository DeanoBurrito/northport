#include <arch/Timers.h>
#include <arch/riscv64/Sbi.h>
#include <config/AcpiTables.h>
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

    void InitGlobalTimers()
    {
        ASSERT(SbiExtensionAvail(SbiExt::Time), "SBI time extension not available.");

        //try get the timer frequency from ACPI first, fall abck to the device tree otherwise.
        using namespace Config;
        bool usedAcpi = true;
        size_t timebaseFrequency;
        if (auto maybeRhct = FindAcpiTable(SigRhct); maybeRhct.HasValue())
        {
            const Rhct* rhct = static_cast<const Rhct*>(*maybeRhct);
            timebaseFrequency = rhct->timebaseFrequency;
        }
        else if (DeviceTree::Global().Available())
        {
            DtProp* prop = DeviceTree::Global().Find("/cpus/")->FindProp("timebase-frequency");
            ASSERT(prop != nullptr, "No DTB node for timebase-frequency");
            timebaseFrequency = prop->ReadValue(1);
            usedAcpi = false;
        }
        else
            ASSERT_UNREACHABLE();

        timerPeriod = sl::ScaledTime::FromFrequency(timebaseFrequency);
        sl::UnitConversion freqUnits = sl::ConvertUnits(timebaseFrequency, sl::UnitBase::Decimal);
        Log("Platform timer: freq=%lu.%lu%shz, obtainedVia=%s", LogLevel::Info, freqUnits.major, 
            freqUnits.minor, freqUnits.prefix, usedAcpi ? "acpi" : "dtb");
    }

    void InitLocalTimers()
    {}

    bool (*timerCallback)(void*);
    void ArmInterruptTimer(size_t nanoseconds, bool (*callback)(void*))
    {
        const size_t triggerTime = RdTime() + (nanoseconds / timerPeriod.ToNanos());
        if (callback != nullptr)
            timerCallback = callback;
        
        SbiSetTimer(triggerTime);
    }

    bool DisarmInterruptTimer()
    {
        SbiSetTimer(-1);
        return true;
    }

    size_t InterruptTimerMaxNanos()
    {
        if (timerPeriod.ToNanos() > 0)
            return (size_t)-1ul; //more nanoseconds than we can count.
        ASSERT_UNREACHABLE();
    }

    size_t PollTimer()
    {
        return RdTime();
    }

    size_t PollTicksToNanos(size_t ticks)
    {
        return ticks * timerPeriod.ToNanos();
    }
    
    const char* InterruptTimerName()
    {
        return "sbi";
    }

    const char* PollTimerName()
    {
        return "rdtime";
    }
}
