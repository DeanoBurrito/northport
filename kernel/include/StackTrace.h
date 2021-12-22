#pragma once

#include <stdint.h>

namespace Kernel 
{
    struct stackframe {
      struct stackframe* ebp;
      uint32_t eip;
    };

    void GetStackTrace();

}
