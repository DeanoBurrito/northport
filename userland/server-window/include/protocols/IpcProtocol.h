#pragma once

#include <protocols/GenericProtocol.h>
#include <containers/Vector.h>
#include <IdAllocator.h>
#include <String.h>

namespace WindowServer
{
    class IpcProtocol : public GenericProtocol
    {
    private:
        sl::UIdAllocator idAlloc;
        sl::Vector<sl::String> returnMailboxes;
        size_t mailboxHandle;

    public:
        IpcProtocol();

        ProtocolType Type() const override;
        void SendPacket(ProtocolClient dest, sl::BufferView packet) override;
        void InjectReceivedPacket(ProtocolClient source, sl::BufferView packet) override;

        void HandlePendingEvents();
        size_t GetIpcHandle() const;
    };

    struct IpcProtocolHeader
    {
        uint64_t key;
    };
}
