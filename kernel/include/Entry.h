#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk
{
    //A temporary hack for early in the kernel init sequence, it allows for VmAlloc()-like
    //functionality before the virtual memory substem is available.
    //Some devices require MMIO to be mapped (x86 apics for example) or there may be
    //firmware features that arent required to be in the hhdm (example: acpi tables, fdt blob),
    //this allows them to be mapped and used. Note that these mappings cannot be unmapped and
    //are not managable by the VM subsystem once running. Use sparingly.
    //The tag is added to the log output to help identify uses of this function
    void* EarlyVmAlloc(uintptr_t paddr, size_t length, bool writable, bool mmio, const char* tag);

    //Provides control over EarlyVmAlloc. By default this feature is disabled. It is also
    //disabled once the VM subsystem is running. If `enable` is true, `newAllocBase` is used
    //to determine where the allocator will start assigning addresses from.
    //It returns the previous value of the highest allocated address.
    uintptr_t EarlyVmControl(bool enable, uintptr_t newAllocBase);

    //started on the first core to finish its local init and begin scheduling.
    void InitThread(void*);

    //called by boot protocol AP entry code.
    void PerCoreEntry(size_t myId);

    //called by boot protocol AP entry code after PerCoreEntry(), and by BSP later on.
    [[noreturn]]
    void ExitCoreInit();
}
