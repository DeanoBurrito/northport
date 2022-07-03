#pragma once

#include <protocols/GenericProtocol.h>
#include <containers/Vector.h>
#include <Optional.h>
#include <String.h>

namespace WindowServer
{
    struct IpcClient
    {
        size_t processId;
        sl::String returnMailbox;
    };
    
    class IpcProtocol : public GenericProtocol
    {
    private:
        sl::Vector<IpcClient> clients;
        size_t mailboxHandle;

        sl::Opt<IpcClient*> GetClient(size_t id);

    public:
        IpcProtocol();

        ProtocolType Type() const override;
        void SendPacket(ProtocolClient dest, sl::BufferView packet) override;
        void InjectReceivedPacket(ProtocolClient source, sl::BufferView packet) override;
        void RemoveClient(ProtocolClient client) override;

        void HandlePendingEvents();
        size_t GetIpcHandle() const;
    };
}
