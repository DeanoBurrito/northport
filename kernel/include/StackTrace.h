#pragma once

#include <stdint.h>
#include <containers/Vector.h>

namespace Kernel 
{

    sl::Vector<NativeUInt> GetStackTrace();
    void PrintStackTrace(sl::Vector<NativeUInt>);

}
