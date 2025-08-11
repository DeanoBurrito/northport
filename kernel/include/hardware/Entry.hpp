#pragma once

#include <Types.hpp>

/* The initial entrypoint to the kernel is defined in BringUp.cpp, these are the
 * runtime entrypoints. The arch layer should call these functions when
 * appropriate.
 */
namespace Npk 
{
    struct PageFaultFrame 
    {
        uintptr_t address;
        bool write;
        bool fetch;
        bool user;
    };

    namespace Debugger 
    {
        enum class EventType;
        enum class DebugError;
    }

    void DispatchAlarm();
    void DispatchIpi();
    void DispatchInterrupt(size_t vector);
    void DispatchPageFault(PageFaultFrame* frame);
    Debugger::DebugError DispatchDebugEvent(Debugger::EventType type, void* data) asm("DispatchDebugEvent");

    struct ThreadContext;

    void BringCpuOnline(ThreadContext* idle);
    // void BringCpuOffline();
}
