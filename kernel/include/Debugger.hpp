#pragma once

#include <lib/List.hpp>
#include <Hardware.hpp>

namespace Npk
{
    namespace Private
    {
        struct Breakpoint;
    }

    enum class DebugEventType
    {
        Init,
        Connect,
        Disconnect,

        Breakpoint,
    };

    enum class BreakpointType
    {
        Breakpoint,
        Manual,
        SingleStep,
    };

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
        /* Human readable name for the protocol, for vanity reasons.
         */
        const char* name;

        /* For internal use by the protocol implementation.
         */
        void* opaque;

        /* Attempts to connect via this protocol to a debug controller on any
         * transport. Upon success the protocol should attach itself to the
         * transport and keep track of which transport it selected.
         */
        NpkStatus (*Connect)(DebugProtocol* inst, DebugTransportList* ports);

        /* Informs the protocol the kernel wants to disconnect from any debug
         * controllers. This function is always assumed to succeed and the
         * debug transport associated with this protocol will be unavailable
         * after this function returns, so the implementation should ensure no
         * further communication with the host is required after returning.
         */
        void (*Disconnect)(DebugProtocol* inst);

        /* Informs the protocol that a breakpoint was hit, `*bp` contains the
         * breakpoint data and can be inspected to determine the exact cause.
         */
        NpkStatus (*BreakpointHit)(DebugProtocol* inst, BreakpointType type,
            Private::Breakpoint* bp, TrapFrame* frame);
    };

    /* Initializes the debugger subsystem, the kernel is **not self debuggable**
     * before this function runs.
     */
    NpkStatus InitDebugger(uintptr_t& virtBase);

    /* Attempts to connect to a debug host via any available transport. If a
     * timeout is provided this function returns `NotAvailable` if a host could
     * not be contacted. Otherwise `Success` is returned.
     */
    NpkStatus ConnectDebugger(sl::TimeCount timeout);

    /* Disconnects the debug host from the current system and resets any
     * local debugger state (e.g. active breakpoints).
     */
    void DisconnectDebugger();

    /* Manual breakpoint, if the debugger is connected this will send a break
     * notification and begin command processing.
     */
    void DebugBreakpoint();

    /* Makes a transport available for use by the kernel debugger.
     */
    NpkStatus AddDebugTransport(DebugTransport* transport);

    /* Used to notify the debug subsystem that an event has occurred. This
     * function must be called with interrupts and preemption disabled.
     */
    extern "C"
    NpkStatus DebugEventOccurred(DebugEventType what, void* data);
}
