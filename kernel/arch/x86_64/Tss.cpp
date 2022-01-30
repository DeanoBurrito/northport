#include <arch/x86_64/Tss.h>
#include <arch/x86_64/Gdt.h>
#include <Platform.h>
#include <Locks.h>

namespace Kernel
{
    char tssInitLock; //taken when editing the gdt tss descriptor
    
    void FlushTSS()
    {
        sl::ScopedSpinlock scopeLock(&tssInitLock);

        SetTssDescriptorAddr(GetCoreLocal()->ptrs[CoreLocalIndices::TSS].raw);
        asm volatile("ltr %0" :: "r"((uint16_t)GDT_ENTRY_TSS));
    }

    TaskStateSegment* CurrentTss()
    {
        return GetCoreLocal()->ptrs[CoreLocalIndices::TSS].As<TaskStateSegment>();
    }
}
