#include <Hardware.hpp>
#include <hardware/x86_64/Private.hpp>

namespace Npk
{
    uintptr_t GetTrapReturnAddr(const TrapFrame* frame)
    {
        return frame->iret.rip;
    }

    uintptr_t GetTrapStackPtr(const TrapFrame* frame)
    {
        return frame->iret.rsp;
    }

    uintptr_t GetTrapBasePtr(const TrapFrame* frame)
    {
        return frame->rbp;
    }

    bool GetTrapIsUserContext(const TrapFrame* frame)
    {
        return frame->iret.cs != 0x8;
    }
}
