#include <StackTrace.h>
#include <Log.h>
#include <containers/Vector.h>

namespace Kernel
{
    void GetStackTrace()
    {
        struct stackframe* stackItem;
        asm ("mov %%rbp,%0" : "=r"(stackItem) ::);
        sl::Vector<NativeUInt> vec;
        while(stackItem != 0){
            Logf("GetStackTrace: %x\n", LogSeverity::Error, stackItem->eip);
            vec.PushBack(stackItem->eip);
            stackItem = stackItem->ebp;
        }
        Logf("GetStackTrace vec size: %x\n", LogSeverity::Error, vec.Size());
    }

}
