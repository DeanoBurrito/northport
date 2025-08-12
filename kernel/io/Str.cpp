#include <Io.hpp>

namespace Npk
{
    sl::StringSpan IoStatusStr(IoStatus status)
    {
        constexpr sl::StringSpan statusStrs[] =
        {
            "invalid",
            "error",
            "pending",
            "continue",
            "complete",
            "shortage",
            "timeout",
        };

        const size_t index = static_cast<size_t>(status);
        if (index > static_cast<size_t>(IoStatus::Timeout))
                return "unknown";
        return statusStrs[index];
    }
    static_assert(static_cast<IoStatus>(0) == IoStatus::Invalid);
    static_assert(static_cast<IoStatus>(1) == IoStatus::Error);
    static_assert(static_cast<IoStatus>(2) == IoStatus::Pending);
    static_assert(static_cast<IoStatus>(3) == IoStatus::Continue);
    static_assert(static_cast<IoStatus>(4) == IoStatus::Complete);
    static_assert(static_cast<IoStatus>(5) == IoStatus::Shortage);
    static_assert(static_cast<IoStatus>(6) == IoStatus::Timeout);

    sl::StringSpan IoTypeStr(IoType type)
    {
        constexpr sl::StringSpan typeStrs[] =
        {
            "invalid",
            "open",
            "close",
            "read",
            "write",
        };

        const size_t index = static_cast<size_t>(type);
        if (index < static_cast<size_t>(IoType::Write))
            return typeStrs[index];
        if (index >= static_cast<size_t>(IoType::VendorBegin))
            return "vendor-specific";
        return "unknown";
    }
    static_assert(static_cast<IoType>(0) == IoType::Invalid);
    static_assert(static_cast<IoType>(1) == IoType::Open);
    static_assert(static_cast<IoType>(2) == IoType::Close);
    static_assert(static_cast<IoType>(3) == IoType::Read);
    static_assert(static_cast<IoType>(4) == IoType::Write);
    static_assert(static_cast<IoType>(1ul << 15) == IoType::VendorBegin);
}
