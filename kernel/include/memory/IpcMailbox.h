#pragma once

#include <NativePtr.h>

namespace Kernel::Memory
{
    struct IpcMailboxControl;

    struct IpcMailHeader
    {
        uint64_t length;
        uint64_t positionInStream; //used to locate mailbox control from a header
        uint8_t data[];

        IpcMailHeader* Next()
        { return sl::NativePtr(this).As<IpcMailHeader>(length + sizeof(IpcMailHeader)); }

        IpcMailboxControl* Control()
        { return sl::NativePtr(this).As<IpcMailboxControl>(-positionInStream); }
    };

    struct IpcMailboxControl
    {
        uint8_t lock;
        uint8_t reserved0[7];
        uint64_t head;
        uint64_t tail;
        uint64_t reserved1;

        IpcMailHeader* First()
        { return sl::NativePtr(this).As<IpcMailHeader>(head); }

        IpcMailHeader* Last()
        { return sl::NativePtr(this).As<IpcMailHeader>(tail); }
    };

    constexpr size_t MailboxDefaultSize = 0x1000;
}
