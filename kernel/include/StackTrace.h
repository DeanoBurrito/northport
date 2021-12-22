#pragma once

#include <stdint.h>

namespace Kernel 
{
    struct stackframe {
      struct stackframe* ebp;
      uint64_t eip;
    };

    void GetStackTrace();

}
