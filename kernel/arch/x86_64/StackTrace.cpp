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
            vec.PushBack(stackItem->eip);
            stackItem = stackItem->ebp;
        }
        return vec;
    }

    void PrintStackTrace()
    {
        sl::Vector<NativeUInt> vec;
        vec = GetStackTrace();
        for(size_t i=0; i<vec.Size(); i++)
        {
            NativeUInt stackItem = vec.PopBack();
            Logf("GetStackTrace: %x\n", LogSeverity::Error, stackItem);
        }
    }

}
