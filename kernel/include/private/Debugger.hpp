#pragma once

#include "../Debugger.hpp"
#include <lib/Flags.hpp>
#include <lib/Locks.hpp>

namespace Npk::Private
{
    constexpr size_t PerCpuStorePointers = 4;
    constexpr char DebugVarNamePrefix = '$';

    struct InitEventArg
    {
        sl::Span<Breakpoint> breakpoints;
        sl::Span<char> logring;
        sl::Span<uintptr_t> perCpu;
    };

    struct ConnectEventArg
    {
        sl::TimeCount timeout;
        DebugTransportList* transports;
    };

    struct DisconnectEventArg
    {
        //intentionally empty for now.
    };

    struct BreakpointEventArg
    {
        BreakpointType type;
        uintptr_t addr;
        TrapFrame* frame;
    };

    struct Breakpoint
    {
        sl::ListHook listHook;
        HwBreakpoint arch;

        uintptr_t addr;
        uintptr_t kind;
        bool read;
        bool write;
        bool execute;
        bool hardware;
    };

    using BreakpointList = sl::List<Breakpoint, &Breakpoint::listHook>;
    using DebugEventTypes = sl::Flags<DebugEventType>;

    struct DebugVariable;

    extern sl::Atomic<DebugEventTypes> debugEventsMask;
    extern DebugProtocol* debugProtocol;
    extern DebugProtocol gdbProtocol;
    extern bool debugHostWantsDisconnect;

    void InitInternalDebuggerApi(InitEventArg* arg);
    void DebuggerPerCpuInit(void* arg);

    /* Logging facility for use inside debugger core. Logs are stored in a
     * central ringbuffer which is overwritten eventually. Log formatting is
     * done using standard C-style printf specifiers.
     */
    void DebuggerLog(const char* format, ...);

    /* Panic function for use inside debugger core. This should be used
     * sparingly since it hangs the system without giving the kernel a change to
     * shutdown gracefully and sync user data.
     * Message formatting uses standard C-style printf specifiers.
     */
    [[noreturn]]
    void DebuggerPanic(const char* format, ...);

    Breakpoint* AllocBreakpoint();
    void FreeBreakpoint(Breakpoint** bp);
    Breakpoint* GetBreakpointByAddr(uintptr_t addr);

    /* Ensures an existing breakpoint is armed and if its conditions are met,
     * the kernel debugger is notified. Returns whether the breakpoint was
     * successfully armed.
     */
    bool ArmBreakpoint(Breakpoint& bp);

    /* Ensures an existing breakpoint will not cause any cpu exceptions or entry
     * to the kernel debugger. Returns whether a breakpoint was successfully
     * disarmed. Note this may return false for a valid breakpoint that already
     * disarmed.
     */
    bool DisarmBreakpoint(Breakpoint& bp);

    /* Disarms and frees all active and allocated breakpoints, useful for when
     * the debug controller disconnectes from the debuggee.
     */
    void ClearAllBreakpoints();

    /* Called by the debug protocol to tell the debugger core that the host
     * has disconnected from the current system. This is similar to the kernel
     * api call `DisconnectDebugger()` but available inside the debugger core.
     */
    void NotifyOfHostDisconnect();

    /* Returns the number of CPU cores known to the debugger.
     */
    size_t GetDebugCpuCount();

    /* Returns a span with `PerCpuStorePointers` entries per CPU core, for
     * debug protocols to store per-cpu state. The layout and contents of these
     * stores is protocol-defined.
     */
    sl::Span<uintptr_t> GetCpuDebugStores();

    /* Returns the frame representing the state when a non-controlling CPU was
     * sent into the freeze loop by the debugger. If this state is unavailable,
     * this function may return `nullptr` (this situation is unlikely).
     */
    TrapFrame* DebugFrameForCpu(CpuId id);

    /*
     */
    NpkStatus CreateDebugVariable(DebugVariable** created, sl::StringSpan name,
        uintptr_t value);

    /*
     */
    NpkStatus DestroyDebugVariable(DebugVariable** var);

    /*
     */
    NpkStatus LookupDebugVariable(DebugVariable** found, sl::StringSpan name);

    /*
     */
    NpkStatus ReadDebugVariable(uintptr_t* value, DebugVariable& var);

    /*
     */
    NpkStatus WriteDebugVariable(DebugVariable& var, uintptr_t value,
        uintptr_t* prevValue);
}
