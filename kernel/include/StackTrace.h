#pragma once

#include <stdint.h>
#include <containers/Vector.h>
#include <Log.h>

namespace Kernel 
{
    sl::Vector<NativeUInt> GetStackTrace(NativeUInt startFrame = 0);
    void PrintStackTrace(sl::Vector<NativeUInt>, LogSeverity severity);
}
