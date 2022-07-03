#pragma once

#include <stdint.h>
#include <Window.h>
#include <BufferView.h>
#include <containers/Vector.h>

namespace np::Gui
{
    class Window;

    struct PendingRequest
    {
        uint64_t type;
        sl::NativePtr data;
    };
    
    class WindowServerClient
    {
    friend Window;
    private:
        uint64_t clientId = 0;
        size_t ipcHandle;
        sl::Vector<PendingRequest> pendingRequests; //TODO: this should be a queue<T>, once that's implemented
        char lock;

        void SendRequest(sl::BufferView buff, sl::NativePtr data);
        void ProcessResponse(sl::BufferView packet);

    public:
        WindowServerClient();
        ~WindowServerClient();
        WindowServerClient(const WindowServerClient& other) = delete;
        WindowServerClient& operator=(const WindowServerClient& other) = delete;
        WindowServerClient(WindowServerClient&& from);
        WindowServerClient& operator=(WindowServerClient&& from);

        bool KeepGoing() const;
        size_t IpcHandle() const;
        void ProcessEvent();
    };
}
