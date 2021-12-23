#include <StackTrace.h>
#include <Log.h>
#include <containers/Vector.h>

namespace Kernel
{    

    struct StackFrame 
    {
        struct StackFrame* rbp;
        uint64_t rip;
    };

    sl::Vector<NativeUInt> GetStackTrace()
    {
        StackFrame* stackItem;
        asm ("mov %%rbp,%0" : "=r"(stackItem));
        sl::Vector<NativeUInt> vec;

        while(stackItem != 0)
        {
            vec.PushBack(stackItem->rip);
            stackItem = stackItem->rbp;
        }

        return vec;
    }

    void PrintStackTrace(sl::Vector<NativeUInt> vec)
    {
        size_t vectorSize = vec.Size();
        Logf("StackTrace: (size: %d)", LogSeverity::Verbose, vec.Size());

        for(size_t i=0; i<vectorSize; i++)
        {
            NativeUInt stackItem = vec.PopBack();
            Logf("Frame %d: ip=%lx", LogSeverity::Verbose, i, stackItem);
        }
    }

}
