#pragma once

#include <protocols/ProtocolClient.h>
#include <BufferView.h>

namespace WindowServer
{
    class WindowManager;
    
    class GenericProtocol
    {
    protected:
        WindowManager* WM();

    public:
        static void RegisterProto(GenericProtocol* protocol);
        static void Send(ProtocolClient client, sl::BufferView packet);
        static void CloseAll();
        static void Remove(ProtocolClient client);

        virtual ProtocolType Type() const = 0;
        virtual void SendPacket(ProtocolClient dest, sl::BufferView packet) = 0;
        //provided as a convinience function for debugging, should ultimately call WM::ProcessPacket
        virtual void InjectReceivedPacket(ProtocolClient source, sl::BufferView packet) = 0;
        //we shouldn't drop the client as soon as we see the 'remove me' message, server may need to send messages first.
        virtual void RemoveClient(ProtocolClient client) = 0;
    };
}
