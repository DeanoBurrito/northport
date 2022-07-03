#include <protocols/IpcProtocol.h>
#include <WindowManager.h>
#include <WindowServerProtocol.h>
#include <SyscallFunctions.h>
#include <Logging.h>

namespace WindowServer
{
    //never check more than this many events at a time. Prevents being blocked by message spam
    constexpr size_t EventProcessLimit = 20;

    sl::Opt<IpcClient*> IpcProtocol::GetClient(size_t id)
    {
        for (size_t i = 0; i < clients.Size(); i++)
        {
            if (clients[i].processId == id)
                return &clients[i];
        }

        return {};
    }
    
    IpcProtocol::IpcProtocol()
    {
        using namespace np::Syscall;
        auto maybeIpcHandle = CreateMailbox(np::Gui::WindowServerMailbox, IpcMailboxFlags::UseSharedMemory | IpcStreamFlags::AccessPublic);
        if (!maybeIpcHandle)
        {
            Log("Unable to start window server IPC protocol, mailbox creation failed.", LogLevel::Error);
            return;
        }

        mailboxHandle = *maybeIpcHandle;
        np::Userland::Log("Window server IPC protocol started, listening at mailbox: %s", LogLevel::Info, np::Gui::WindowServerMailbox);
    }
    
    ProtocolType IpcProtocol::Type() const
    { return ProtocolType::Ipc; }

    void IpcProtocol::SendPacket(ProtocolClient dest, sl::BufferView packet)
    {
        if (dest.protocol != Type())
            return;
        if (packet.base.ptr == nullptr || packet.length == 0)
            return;

        auto maybeClient = GetClient(dest.clientId);
        if (!maybeClient)
            return;
        
        np::Syscall::PostToMailbox(maybeClient.Value()->returnMailbox, packet);
    }

    void IpcProtocol::InjectReceivedPacket(ProtocolClient source, sl::BufferView packet)
    {
        WM()->ProcessPacket({ Type(), source.clientId }, packet);
    }

    void IpcProtocol::RemoveClient(ProtocolClient client)
    { np::Syscall::Log("Not implemented() IpcProtocol::RemoveClien()", LogLevel::Warning); }

    void IpcProtocol::HandlePendingEvents()
    {
        size_t eventsProcessed = 0;
        auto maybeEvent = np::Syscall::PeekNextEvent();
        if (!maybeEvent)
            return;

        while (eventsProcessed < EventProcessLimit)
        {
            //there is the potential for crashing the window server by overflowing the stack here.
            //if you're reading this, no I am not awarding a bug bounty for this.
            uint8_t packetBuffer[maybeEvent->dataLength];
            const np::Syscall::ProgramEvent event = np::Syscall::ConsumeNextEvent({ packetBuffer, maybeEvent->dataLength }).Value();
            
            auto maybeClient = GetClient(event.sender);
            if (!maybeClient)
            {
                const np::Gui::NewClientRequest* request = reinterpret_cast<np::Gui::NewClientRequest*>(packetBuffer);
                if (request->type == np::Gui::RequestType::NewClient)
                {
                    //non-existing client, must be a new request.
                    np::Userland::Log("New IPC window client: sender=%u, mailbox=%s", LogLevel::Verbose, event.sender, request->responseAddr);

                    IpcClient ipcClient;
                    ipcClient.processId = event.sender;
                    //TODO: we assume the string is null-terminated, we should only read until the end of the buffer
                    ipcClient.returnMailbox = sl::String(reinterpret_cast<const char*>(request->responseAddr));
                    clients.PushBack(ipcClient);
                    
                    WM()->ProcessPacket({ Type(), ipcClient.processId}, { packetBuffer, event.dataLength });
                }
                else
                    np::Userland::Log("IPC protocol received unconnected message from client: type=%u", LogLevel::Error, request->type);
            }
            else
            {
                //existing client, process as normal
                const ProtocolClient incomingClient(Type(), maybeClient.Value()->processId);
                WM()->ProcessPacket(incomingClient, { packetBuffer, event.dataLength});
            }
            
            //check if there's any more messages for us.
            maybeEvent = np::Syscall::PeekNextEvent();
            eventsProcessed++;
            if (!maybeEvent || maybeEvent->type != np::Syscall::ProgramEventType::IncomingMail || maybeEvent->handle != mailboxHandle)
                return;
        }
    }

    size_t IpcProtocol::GetIpcHandle() const
    { return mailboxHandle; }
}
