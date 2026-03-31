#include <private/Process.hpp>

namespace Npk
{
    NpkStatus CreateProcess(Process** proc, Job& parent)
    {
        return NpkStatus::Unsupported;
    }

    void UnrefProcess(Process& proc)
    {
        UnrefObject(proc.nsObj);
    }
    static_assert(offsetof(Process, nsObj) == 0);
}
