#include <Status.hpp>

namespace Npk
{
    const char* StatusStr(NpkStatus what)
    {
        switch (what)
        {
        case NpkStatus::Success:
            return "success";
        case NpkStatus::InternalError:
            return "internal error";
        case NpkStatus::Shortage:
            return "shortage";
        case NpkStatus::InvalidArg:
            return "invalid argument";
        case NpkStatus::BadObject:
            return "bad object";
        case NpkStatus::BadVaddr:
            return "bad vaddr";
        case NpkStatus::InUse:
            return "in use";
        case NpkStatus::ObjRefFailed:
            return "object reference failed";
        case NpkStatus::LockAcquireFailed:
            return "lock acquire failed";
        case NpkStatus::Unsupported:
            return "unsupported";
        default:
            return "<>";
        }
    }
}
