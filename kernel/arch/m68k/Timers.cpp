#include <arch/Timers.h>
#include <debug/Log.h>
#include <memory/VmObject.h>
#include <interrupts/Router.h>

namespace Npk
{
    enum TimerReg
    {
        TimeLow = 0x0,
        TimeHigh = 0x4,
        AlarmLow = 0x8,
        AlarmHigh = 0xC,
        IrqEnabled = 0x10,
        ClearAlarm = 0x14,
        AlarmStatus = 0x18,
        ClearIrq = 0x1C,
    };

    constexpr uintptr_t QemuGoldfishPaddr = 0xff006000; //TODO: pull this from bootloader
    constexpr size_t GoldfishTimerIrq = 160; //TODO: also pull from bootloader
    VmObject timerRegs;
    InterruptRoute timerIntrRoute;

    void InitTimers()
    {
        timerRegs = VmObject(0x1000, QemuGoldfishPaddr, VmFlag::Mmio | VmFlag::Write);
        ASSERT_(timerRegs.Valid());
        timerRegs->Offset(IrqEnabled).Write<uint32_t>(1);

        timerIntrRoute.Callback = nullptr;
        timerIntrRoute.dpc = nullptr;
        ASSERT_(ClaimInterruptRoute(&timerIntrRoute, CoreLocal().id, GoldfishTimerIrq));
    }

    void SetSysTimer(size_t nanoseconds, bool (*callback)(void*))
    {
        if (callback != nullptr)
            timerIntrRoute.Callback = callback;

        uint64_t target = timerRegs->Offset(TimerReg::TimeLow).Read<uint32_t>();
        target |= static_cast<uint64_t>(timerRegs->Offset(TimerReg::TimeHigh).Read<uint32_t>()) << 32;
        target += nanoseconds;

        timerRegs->Offset(TimerReg::AlarmHigh).Write<uint32_t>(target >> 32);
        timerRegs->Offset(TimerReg::AlarmLow).Write<uint32_t>(target & 0xFFFF'FFFF);
    }

    size_t SysTimerMaxNanos()
    {
        return (size_t)-1;
    }

    size_t PollTimer()
    {
        size_t accum = timerRegs->Offset(TimerReg::TimeLow).Read<uint32_t>();
        accum |= static_cast<uint64_t>(timerRegs->Offset(TimerReg::TimeHigh).Read<uint32_t>()) << 32;

        return accum;
    }

    size_t PolledTicksToNanos(size_t ticks)
    {
        return ticks;
    }
    
    const char* SysTimerName()
    {
        return "goldfish rtc";
    }

    const char* PollTimerName()
    {
        return "goldfish rtc";
    }
}
