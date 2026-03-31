#include <private/Process.hpp>
#include <Namespace.hpp>

namespace Npk
{
    NpkStatus CreateThread(Thread** thread, Process& parent)
    {
        return NpkStatus::Unsupported;
    }

    void UnrefThread(Thread& thread)
    {
        UnrefObject(thread.nsObj);
    }
    static_assert(offsetof(Thread, nsObj) == 0);
}
