#pragma once

#include <stdint.h>

namespace Kernel::Filesystem
{
    struct FileDetails
    {
    private:
    public:
        const uint64_t filesize;

        FileDetails(uint64_t size) 
        : filesize(size)
        {}
    };
}
