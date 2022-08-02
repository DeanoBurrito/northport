#pragma once

#include <BufferView.h>

namespace Kernel::Devices
{
    //creates a useful buffer for block devices. Auto-frees the physical
    //memory when going out of scope.
    struct IoBlockBuffer
    {
        sl::BufferView memory;

        IoBlockBuffer(size_t pages);
        ~IoBlockBuffer();

        IoBlockBuffer(const IoBlockBuffer&) = delete;
        IoBlockBuffer& operator=(const IoBlockBuffer&) = delete;
    };
}
