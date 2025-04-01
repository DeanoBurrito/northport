#pragma once

#include <Types.h>

/* The initial entrypoint to the kernel is defined in BringUp.cpp, these are the
 * runtime entrypoints. The arch layer should call these functions when
 * appropriate.
 */
namespace Npk
{
    struct SyscallFrame
    {
        constexpr static size_t ArgCount = 6;

        void* args[ArgCount];
        void* rets[ArgCount];
    };

    enum class ExceptionType : uintptr_t
    {
        BadOp = 0,
        Breakpoint = 1,
    };

    struct ExceptionFrame
    {
        ExceptionType type;
        uintptr_t pc;
        uintptr_t stack;
        uintptr_t archId;
        uintptr_t archFlags;
    };

    struct PageFaultFrame
    {
        uintptr_t address;
        bool write;
        bool fetch;
        bool user;
    };

    void DispatchAlarm();
    void DispatchIpi();
    void DispatchInterrupt(size_t vector);
    void DispatchPageFault(PageFaultFrame* frame);
    void DispatchSyscall(SyscallFrame* frame);
    void DispatchException(ExceptionFrame* frame);
}
