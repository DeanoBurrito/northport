#include <DebuggerPrivate.hpp>

namespace Npk::Private
{
    struct GdbData
    {
    } gdbData;

    DebugStatus GdbConnect(DebugProtocol* inst, DebugTransportList ports)
    {
        //TODO: scan all transports until we get a valid packet from one of them.
        SL_UNREACHABLE();
    }

    void GdbDisconnect(DebugProtocol* inst)
    {
        SL_UNREACHABLE();
    }

    DebugProtocol gdbProtocol
    {
        .name = "gdb-remote",
        .opaque = &gdbData,
        .Connect = GdbConnect,
        .Disconnect = GdbDisconnect,
    };
}
