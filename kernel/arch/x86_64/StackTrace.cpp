#include <StackTrace.h>
#include <Log.h>
#include <containers/Vector.h>
#include <elf/HeaderParser.h>

namespace Kernel
{    
    struct StackFrame 
    {
        StackFrame* rbp;
        uint64_t rip;
    };

    sl::Vector<NativeUInt> GetStackTrace(NativeUInt startFrame)
    {
        StackFrame* stackItem;
        if (startFrame == 0)
            asm ("mov %%rbp,%0" : "=r"(stackItem));
        else
            stackItem = reinterpret_cast<StackFrame*>(startFrame);
        sl::Vector<NativeUInt> vec;

        while(stackItem->rip != 0)
        {
            vec.PushBack(stackItem->rip);
            stackItem = stackItem->rbp;
        }

        return vec;
    }

    void PrintStackTrace(sl::Vector<NativeUInt> vec, LogSeverity severity)
    {
        const size_t vecSize = vec.Size();
        sl::Elf64HeaderParser symbolStore(currentProgramElf);

        for(size_t i = 0; i < vecSize; i++)
        {
            NativeUInt stackItem = vec.PopBack();
            string symbolName = currentProgramElf.ptr == nullptr ? "<no symbol store>" : symbolStore.GetSymbolName(stackItem);
            Logf("Frame %d: ip=%lx, symbolName=%s", severity, i, stackItem, symbolName.C_Str());
        }
    }
}
