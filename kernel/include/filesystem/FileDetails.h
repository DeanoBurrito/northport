#pragma once

#include <stddef.h>

namespace Kernel::Filesystem
{
    struct FileDetails
    {
    private:
    public:
        const size_t filesize;

        FileDetails(size_t size) 
        : filesize(size)
        {}
    };
}
