#include <StackTrace.h>
#include <Log.h>
#include <containers/Vector.h>

namespace Kernel
{
    sl::Vector<NativeUInt> GetStackTrace()
    {
        struct stackframe* stackItem;
        asm ("mov %%rbp,%0" : "=r"(stackItem) ::);
        sl::Vector<NativeUInt> vec;
        while(stackItem != 0)
        {
            vec.PushBack(stackItem->rip);
            stackItem = stackItem->rbp;
        }
        return vec;
    }

    void PrintStackTrace()
    {
        sl::Vector<NativeUInt> vec;
        vec = GetStackTrace();
        size_t vectorSize = vec.Size();
        Logf("GetStackTrace: size: %x\n", LogSeverity::Error, vec.Size());
        for(size_t i=0; i<vectorSize; i++)
        {
            NativeUInt stackItem = vec.PopBack();
            Logf("GetStackTrace(Size: %d)  eip: %d: - %x\n", LogSeverity::Error, vectorSize, i,stackItem);
        }
    }

}
