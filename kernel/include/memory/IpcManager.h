#pragma once

#include <stddef.h>
#include <String.h>
#include <NativePtr.h>
#include <Optional.h>
#include <IdAllocator.h>
#include <containers/Vector.h>

namespace Kernel::Memory
{
    enum class IpcStreamFlags
    {
        None = 0,
        UseSharedMemory = (1 << 0),
    };

    enum class IpcAccessFlags : uint8_t
    {
        Disallowed = 0,
        Public = 1,
        SelectedOnly = 2,
        Private = 3,
    };
    
    struct IpcStream
    {
        IpcStreamFlags flags;
        sl::NativePtr bufferAddr;
        size_t bufferLength;
        size_t ownerId;
        const sl::String name;

        IpcAccessFlags accessFlags;
        sl::Vector<size_t> accessList; // thread/process ids that can access this stream.

        IpcStream(const sl::String& name) : name(name) {}
    };
    
    class IpcManager
    {
    private:
        sl::UIdAllocator* idAlloc;
        sl::Vector<IpcStream*>* streams;
        char lock;

    public:
        static IpcManager* Global();

        void Init();

        sl::Opt<IpcStream*> StartStream(const sl::String& name, size_t length, IpcStreamFlags flags, IpcAccessFlags accessFlags);
        void StopStream(const sl::String& name);
        sl::Opt<sl::NativePtr> OpenStream(const sl::String& name, IpcStreamFlags flags);
        void CloseStream(const sl::String& name);
    };
}
