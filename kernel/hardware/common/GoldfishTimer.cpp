#include <hardware/common/GoldfishTimer.hpp>
#include <Vm.hpp>
#include <Mmio.hpp>

namespace Npk
{
    enum class TimerReg
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

    using TimerRegisters = sl::MmioRegisters<TimerReg, uint32_t>;

    static bool gfTimerAvailable = false;
    static TimerRegisters gfTimerRegs;

    bool InitGoldfishTimer(uintptr_t& virtBase, Paddr foundAt)
    {
        if (ReadConfigUint("npk.ignore_goldfish_timer", false))
            return false;
        if (ReadConfigUint("npk.ignore_goldfish_rtc", false))
            return false;

        const size_t offset = foundAt & PageMask();

        auto status = SetKernelMap(virtBase, foundAt, 
            VmFlag::Write | VmFlag::Mmio);
        if (status != VmStatus::Success)
            return false;

        gfTimerRegs = virtBase + offset;
        virtBase += PageSize();

        Log("Goldfish timer/rtc @ 0x%tx (%p)", LogLevel::Info, foundAt,
            gfTimerRegs.BasePointer());

        return true;
    }

    bool GoldfishTimerAvailable()
    {
        return gfTimerAvailable;
    }

    uint64_t GoldfishTimerRead()
    {
        uint64_t low = gfTimerRegs.Read(TimerReg::TimeLow);
        uint64_t high = gfTimerRegs.Read(TimerReg::TimeHigh);

        return low | (high << 32);
    }

    void GoldfishTimerArm(uint64_t timestamp)
    {
        gfTimerRegs.Write(TimerReg::AlarmHigh, timestamp >> 32);
        gfTimerRegs.Write(TimerReg::AlarmLow, timestamp & 0xFFFF'FFFF);
    }
}
