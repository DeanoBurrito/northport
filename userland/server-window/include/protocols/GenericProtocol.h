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

        virtual ProtocolType Type() const = 0;
        virtual void SendPacket(ProtocolClient dest, sl::BufferView packet) = 0;
        //provided as a convinience function for debugging, should ultimately call WM::ProcessPacket
        virtual void InjectReceivedPacket(ProtocolClient source, sl::BufferView packet) = 0;
    };
}
