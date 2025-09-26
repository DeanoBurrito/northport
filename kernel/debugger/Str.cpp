#include <Debugger.hpp>

namespace Npk
{
    sl::StringSpan DebugEventTypeStr(DebugEventType which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "init",
            "connect",
            "disconnect",
            "breakpoint",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(DebugEventType::Breakpoint))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<DebugEventType>(0) == DebugEventType::Init);
    static_assert(static_cast<DebugEventType>(1) == DebugEventType::Connect);
    static_assert(static_cast<DebugEventType>(2) == DebugEventType::Disconnect);
    static_assert(static_cast<DebugEventType>(3) == DebugEventType::Breakpoint);

    sl::StringSpan DebugStatusStr(DebugStatus which)
    {
        constexpr sl::StringSpan strs[] =
        {
            "success",
            "not-supported",
            "invalid-argument",
            "bad-environment",
        };

        const size_t index = static_cast<size_t>(which);
        if (index > static_cast<size_t>(DebugStatus::BadEnvironment))
            return "unknown";
        return strs[index];
    }
    static_assert(static_cast<DebugStatus>(0) == DebugStatus::Success);
    static_assert(static_cast<DebugStatus>(1) == DebugStatus::NotSupported);
    static_assert(static_cast<DebugStatus>(2) == DebugStatus::InvalidArgument);
    static_assert(static_cast<DebugStatus>(3) == DebugStatus::BadEnvironment);
}
