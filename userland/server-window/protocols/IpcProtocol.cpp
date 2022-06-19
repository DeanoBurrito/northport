#include <protocols/IpcProtocol.h>
#include <WindowManager.h>
#include <WindowServerProtocol.h>
#include <SyscallFunctions.h>
#include <Logging.h>

namespace WindowServer
{
    //never check more than this many events at a time. Prevents being blocked by message spam
    constexpr size_t EventProcessLimit = 20;
    
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
        if (dest.clientId >= returnMailboxes.Size() || returnMailboxes[dest.clientId].IsEmpty())
            return;
        if (packet.base.ptr == nullptr || packet.length == 0)
            return;
        
        np::Syscall::PostToMailbox(returnMailboxes[dest.clientId], packet);
    }

    void IpcProtocol::InjectReceivedPacket(ProtocolClient source, sl::BufferView packet)
    {
        WM()->ProcessPacket({ Type(), source.clientId }, packet);
    }

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
            np::Syscall::ConsumeNextEvent({ packetBuffer, maybeEvent->dataLength });
            
            sl::NativePtr packetOp(packetBuffer);
            const IpcProtocolHeader* header = packetOp.As<IpcProtocolHeader>();
            const ProtocolClient clientHandle(Type(), header->key);
            packetOp.raw += sizeof(IpcProtocolHeader);

            if (clientHandle.clientId == 0 && packetOp.As<np::Gui::RequestBase>()->type == np::Gui::RequestType::NewClient)
            {
                const np::Gui::NewClientRequest* request = packetOp.As<np::Gui::NewClientRequest>();
                np::Userland::Log("New IPC window client, return mailbox: %s", LogLevel::Verbose, request->responseAddr);

                const size_t newClientId = idAlloc.Alloc();
                returnMailboxes.EnsureCapacity(newClientId + 1);
                returnMailboxes[newClientId] = sl::String(reinterpret_cast<const char*>(request->responseAddr));
                WM()->ProcessNewClient({ Type(), newClientId }, request);
            }
            else if (clientHandle.clientId >= returnMailboxes.Size() || returnMailboxes[clientHandle.clientId].IsEmpty())
                Log("Received IPC protocol message from unknown client.", LogLevel::Warning);
            else
                WM()->ProcessPacket(clientHandle, { packetOp, maybeEvent->dataLength - sizeof(IpcProtocolHeader) });

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
