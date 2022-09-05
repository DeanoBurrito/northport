#include <WindowServerProtocol.h>
#include <WindowServerClient.h>
#include <SyscallFunctions.h>
#include <Logging.h>
#include <Format.h>
#include <Memory.h>
#include <Locks.h>

namespace np::Gui
{
    constexpr size_t EventsProcessedLimit = 20;

    void WindowServerClient::SendRequest(sl::BufferView buff, sl::NativePtr data)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        const uint64_t type = *buff.base.As<uint64_t>();
        pendingRequests.PushBack({ type, data });
        np::Syscall::PostToMailbox(WindowServerMailbox, buff);
    }

    void WindowServerClient::ProcessResponse(sl::BufferView packet)
    {
        const ResponseBase* respBase = packet.base.As<ResponseBase>();
        PendingRequest request = pendingRequests.Front();

        switch (respBase->type)
        {
        case ResponseType::GeneralAcknowledge:
            pendingRequests.Erase(0);
            break;
        
        case ResponseType::GeneralError:
        {
            const GeneralErrorResponse* error = static_cast<const GeneralErrorResponse*>(respBase);
            Userland::Log("WindowServerClient failed operation of type %u, error code %u.", LogLevel::Error, request.type, error->code);
            pendingRequests.Erase(0);
            break;
        }
    
        case ResponseType::AcknowledgeValue:
        {
            const AcknowledgeValueResponse* ack = static_cast<const AcknowledgeValueResponse*>(respBase);
            pendingRequests.Erase(0);
            break;
        }

        default:
            Userland::Log("WindowServerClient received unexpected response from server: type=0x%x", LogLevel::Warning, respBase->type);
            break;
        }
    }
    
    WindowServerClient::WindowServerClient()
    {
        using namespace Syscall;
        clientId = GetId(np::Syscall::GetIdType::ThreadGroup);

        const sl::String responseMailbox = sl::FormatToString("window-client-%u/incoming", 0, clientId);
        const IpcStreamFlags flags = sl::EnumSetFlag(IpcStreamFlags::UseSharedMemory, IpcStreamFlags::AccessPublic);
        auto maybeMailbox = CreateMailbox(responseMailbox, flags);
        if (!maybeMailbox)
        {
            Log("Could not create response mailbox for WindowServerClient, aborting ctor.", LogLevel::Error);
            return;
        }
        ipcHandle = *maybeMailbox;

        NewClientRequest request;
        sl::memcopy(responseMailbox.C_Str(), request.responseAddr, responseMailbox.Size() + 1); //+1 because we want to include the null-terminator

        //we may be an early program, and have reached this point before the window server is ready.
        //if the server is ready, this'll succeed the first try (which is the normal).
        while (!PostToMailbox(WindowServerMailbox, { &request, sizeof(NewClientRequest) }))
            Sleep(1, false);

        sl::SpinlockRelease(&lock);
    }

    WindowServerClient::~WindowServerClient()
    {
        if (clientId == 0)
            return;
        
        RemoveClientRequest request;
        np::Syscall::PostToMailbox(WindowServerMailbox, { &request, sizeof(RemoveClientRequest) });
        clientId = 0;
    }

    WindowServerClient::WindowServerClient(WindowServerClient&& from)
    {
        sl::Swap(clientId, from.clientId);
        sl::Swap(ipcHandle, ipcHandle);
    }

    WindowServerClient& WindowServerClient::operator=(WindowServerClient&& from)
    {
        sl::Swap(clientId, from.clientId);
        sl::Swap(ipcHandle, ipcHandle);

        return *this;
    }

    bool WindowServerClient::KeepGoing() const
    {
        return true;
    }

    size_t WindowServerClient::IpcHandle() const
    { return ipcHandle; }

    void WindowServerClient::ProcessEvent()
    {
        auto maybeEvent = Syscall::PeekNextEvent();
        size_t processedCount = 0;

        while (maybeEvent && processedCount < EventsProcessedLimit)
        {
            if (maybeEvent->type != Syscall::ProgramEventType::IncomingMail)
                return;
            if (maybeEvent->handle != ipcHandle)
                return;
            
            uint8_t packetBuffer[maybeEvent->dataLength];
            const auto event = Syscall::ConsumeNextEvent({ packetBuffer, maybeEvent->dataLength });
            //TODO: we should check the sender's id is actually the window server,
            //and not just a random process sending us messages.

            ProcessResponse({ packetBuffer, maybeEvent->dataLength });
            
            processedCount++;
            maybeEvent = Syscall::PeekNextEvent();
        }
    }
}
