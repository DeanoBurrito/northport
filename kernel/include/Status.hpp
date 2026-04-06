#pragma once

#include <Types.hpp>

namespace Npk
{
    enum class NpkStatus
    {
        Success,
        InternalError,
        Shortage,
        InvalidArg,
        BadObject,
        BadVaddr,
        InUse,
        ObjRefFailed,
        LockAcquireFailed,
        Unsupported,
        AlreadyMapped,
        NotAvailable,
        Busy,
        NotWritable,
        BadConfig,
        NotFound,
    };

    const char* StatusStr(NpkStatus what);
}
