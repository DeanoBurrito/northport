#pragma once

#include <stdint.h>
#include <containers/Vector.h>

namespace Kernel 
{
    struct stackframe 
    {
      struct stackframe* rbp;
      uint64_t rip;
    };

    sl::Vector<NativeUInt> GetStackTrace();
    void PrintStackTrace();

}
