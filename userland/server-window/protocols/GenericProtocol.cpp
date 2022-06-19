#include <protocols/GenericProtocol.h>
#include <containers/Vector.h>
#include <Logging.h>

namespace WindowServer
{
    WindowManager* globalWindowManager;
    sl::Vector<GenericProtocol*> protos;

    WindowManager* GenericProtocol::WM()
    { return globalWindowManager; }

    void GenericProtocol::RegisterProto(GenericProtocol* protocol)
    {
        const size_t index = (size_t)protocol->Type();
        if (index < protos.Size() && protos[index] != nullptr)
            return;
        
        while (index >= protos.Size())
            protos.EmplaceBack(nullptr);
        
        protos[index] = protocol;
        np::Userland::Log("Window server registered new protocol, type=%u", LogLevel::Info, index);
    }

    void GenericProtocol::Send(ProtocolClient client, sl::BufferView packet)
    {
        const size_t protoIndex = (size_t)client.protocol;
        if (protoIndex < protos.Size() && protos[protoIndex] != nullptr)
            protos[protoIndex]->Send(client, packet);
    }

    void GenericProtocol::CloseAll()
    {}
}
