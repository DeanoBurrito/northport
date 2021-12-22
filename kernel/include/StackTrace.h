#pragma once

#include <stdint.h>
#include <containers/Vector.h>

namespace Kernel 
{
    struct stackframe 
    {
      struct stackframe* ebp;
      uint64_t eip;
    };

    sl::Vector<NativeUInt> GetStackTrace();

}
