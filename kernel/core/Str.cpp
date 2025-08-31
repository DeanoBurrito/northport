#include <Core.hpp>

/* Yes this is super ugly and a very low-tech way of doing this, however I
 * in lieu of language support for this, all the options for generating this
 * code automatically involved various hacks or macros, which I'm not willing
 * to pollute the public API headers with.
 * I would prefer that they remain simple and straight forward to understand,
 * and have this mess in the implementation.
 * There are also sometimes oddities like a vendor-specific range of values,
 * I was unable to find a macro-based solution that would handle that.
 * 
 * Each subsystem has its own `Str.cpp` file, the same logic applies there.
 */
namespace Npk
{
    sl::StringSpan IplStr(Ipl which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "passive",
            "dpc",
            "interrupt"
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(Ipl::Interrupt))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<Ipl>(0) == Ipl::Passive);
    static_assert(static_cast<Ipl>(1) == Ipl::Dpc);
    static_assert(static_cast<Ipl>(2) == Ipl::Interrupt);

    sl::StringSpan ConfigRootTypeStr(ConfigRootType which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "rsdp",
            "fdt",
            "bootinfo",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(ConfigRootType::BootInfo))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<Ipl>(0) == Ipl::Passive);
    static_assert(static_cast<Ipl>(1) == Ipl::Dpc);
    static_assert(static_cast<Ipl>(2) == Ipl::Interrupt);

    sl::StringSpan CycleAccountStr(CycleAccount which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "user",
            "kernel",
            "kernel-interrupt",
            "driver",
            "driver-interrupt",
            "debugger",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(CycleAccount::Debugger))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<CycleAccount>(0) == CycleAccount::User);
    static_assert(static_cast<CycleAccount>(1) == CycleAccount::Kernel);
    static_assert(static_cast<CycleAccount>(2) == CycleAccount::KernelInterrupt);
    static_assert(static_cast<CycleAccount>(3) == CycleAccount::Driver);
    static_assert(static_cast<CycleAccount>(4) == CycleAccount::DriverInterrupt);
    static_assert(static_cast<CycleAccount>(5) == CycleAccount::Debugger);

    sl::StringSpan WaitStatusStr(WaitStatus which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "incomplete",
            "timedout",
            "reset",
            "cancelled",
            "success",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(WaitStatus::Success))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<WaitStatus>(0) == WaitStatus::Incomplete);
    static_assert(static_cast<WaitStatus>(1) == WaitStatus::Timedout);
    static_assert(static_cast<WaitStatus>(2) == WaitStatus::Reset);
    static_assert(static_cast<WaitStatus>(3) == WaitStatus::Cancelled);
    static_assert(static_cast<WaitStatus>(4) == WaitStatus::Success);

    sl::StringSpan WaitableTypeStr(WaitableType which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "condition",
            "timer",
            "mutex",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(WaitableType::Mutex))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<WaitableType>(0) == WaitableType::Condition);
    static_assert(static_cast<WaitableType>(1) == WaitableType::Timer);
    static_assert(static_cast<WaitableType>(2) == WaitableType::Mutex);

    sl::StringSpan LogLevelStr(LogLevel level)
    {
        constexpr sl::StringSpan levelStrs[] =
        {
            "Error",
            "Warning",
            "Info",
            "Verbose",
            "Trace",
            "Debug",
        };

        if (static_cast<size_t>(level) > static_cast<size_t>(LogLevel::Debug))
            return "unknown";
        return levelStrs[static_cast<size_t>(level)];
    }
    static_assert(static_cast<LogLevel>(0) == LogLevel::Error);
    static_assert(static_cast<LogLevel>(1) == LogLevel::Warning);
    static_assert(static_cast<LogLevel>(2) == LogLevel::Info);
    static_assert(static_cast<LogLevel>(3) == LogLevel::Verbose);
    static_assert(static_cast<LogLevel>(4) == LogLevel::Trace);
    static_assert(static_cast<LogLevel>(5) == LogLevel::Debug);

    sl::StringSpan ThreadStateStr(ThreadState which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "dead",
            "standby",
            "ready",
            "executing",
            "waiting",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(ThreadState::Waiting))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<ThreadState>(0) == ThreadState::Dead);
    static_assert(static_cast<ThreadState>(1) == ThreadState::Standby);
    static_assert(static_cast<ThreadState>(2) == ThreadState::Ready);
    static_assert(static_cast<ThreadState>(3) == ThreadState::Executing);
    static_assert(static_cast<ThreadState>(4) == ThreadState::Waiting);
}
