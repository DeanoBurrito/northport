#pragma once

#include <Types.hpp>
#include <Span.hpp>
#include <containers/List.hpp>

namespace Npk
{
    constexpr size_t MaxBreakpointDataSize = 8;

    enum class DebugEventType
    {
        Init,
        Connect,
        Disconnect,

        Breakpoint,
    };

    enum class DebugStatus
    {
        Success,
        NotSupported,
        InvalidArgument,
        BadEnvironment,
        InvalidBreakpoint,
    };

    struct Breakpoint
    {
        sl::ListHook listHook;
        uint8_t backupStore[MaxBreakpointDataSize];

        uintptr_t addr;
        union
        {
            uint8_t length;
            uint8_t kind;
        };

        bool read;
        bool write;
        bool execute;
        bool hardware;
        uint8_t hwBind;
    };

    using BreakpointList = sl::List<Breakpoint, &Breakpoint::listHook>;

    struct DebugTransport
    {
        /* Reserved for internal debugger use.
         */
        sl::ListHook hook;

        /* Multiple debug transports can be exposed to the debugger, when
         * attempting to connect to a debug host each transport will be tried
         * from highest priority to lowest. This field should be set before
         * calling `AddDebugTransport()`.
         */
        size_t priority;

        /* Used to represent the transport in logs, informational only. This
         * field should be set before calling `AddDebugTransport()`.
         */
        const char* name;

        /* This pointer is reserved for use by the debug transport provider
         * (a caller of `AddDebugTransport()`).
         */
        void* opaque;

        /* Attempts to send a buffer (`data`) over this transport. Returns if
         * sending was successful/encountered no errors.
         */
        bool (*Transmit)(DebugTransport* inst, sl::Span<const uint8_t> data);

        /* Attempts read a number of bytes into `buffer`, respecting its size.
         */
        size_t (*Receive)(DebugTransport* inst, sl::Span<uint8_t> buffer);
    };

    using DebugTransportList = sl::List<DebugTransport, &DebugTransport::hook>;

    struct DebugProtocol
    {
        const char* name;
        void* opaque;

        DebugStatus (*Connect)(DebugProtocol* inst, DebugTransportList* ports);
        void (*Disconnect)(DebugProtocol* inst);
        DebugStatus (*BreakpointHit)(DebugProtocol* inst, Breakpoint* bp);
    };

    /* Initializes the debugger subsystem, the kernel is **not self debuggable**
     * before this function runs.
     */
    void InitDebugger();

    /* Spins until a debug host can be contacted via an available transport.
     */
    DebugStatus ConnectDebugger();

    /* Disconnects the debug host from the current system and resets any
     * local debugger state (e.g. active breakpoints).
     */
    void DisconnectDebugger();

    /* Makes a transport available for use by the kernel debugger.
     */
    void AddDebugTransport(DebugTransport* transport);

    /* Used to notify the debug subsystem that an event has occured. This
     * function must be called with interrupts and preemption disabled.
     */
    extern "C"
    DebugStatus DebugEventOccured(DebugEventType what, void* data);

    sl::StringSpan DebugEventTypeStr(DebugEventType which);
    sl::StringSpan DebugStatusStr(DebugStatus which);
}
