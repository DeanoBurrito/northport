#include <StackTrace.h>
#include <Log.h>

namespace Kernel
{
    void GetStackTrace()
    {
        struct stackframe* stackItem;
        asm ("mov %%rbp,%0" : "=r"(stackItem) ::);
        while(stackItem != 0){
            Logf("GetStackTrace: %x\n", LogSeverity::Error, stackItem->eip);
            stackItem = stackItem->ebp;
        }
    }

}
