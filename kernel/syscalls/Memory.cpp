#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <scheduling/Scheduler.h>

namespace Kernel::Syscalls
{
    Memory::MemoryMapFlags ParseFlags(np::Syscall::MemoryMapFlags flags, bool forceUserAccess, bool systemRegion)
    {
        using InFlags = np::Syscall::MemoryMapFlags;
        using OutFlags = Memory::MemoryMapFlags;
        
        size_t retFlags = 0;
        if (forceUserAccess || sl::EnumHasFlag(flags, InFlags::UserVisible))
            retFlags |= (size_t)OutFlags::UserAccessible;
        if (systemRegion)
            retFlags |= (size_t)OutFlags::SystemRegion;
        if (sl::EnumHasFlag(flags, InFlags::Executable))
            retFlags |= (size_t)OutFlags::AllowExecute;
        if (sl::EnumHasFlag(flags, InFlags::Writable))
            retFlags |= (size_t)OutFlags::AllowWrites;

        return (OutFlags)retFlags;
    }
    
    void MapMemory(SyscallRegisters& regs)
    {
        Scheduling::Thread* currentThread = Scheduling::Thread::Current();
        bool isUserThread = !sl::EnumHasFlag(currentThread->Flags(), Scheduling::ThreadFlags::KernelMode);
        uint64_t mapBase = regs.arg0;
        uint64_t mapLength = (regs.arg1 / PAGE_FRAME_SIZE + 1) * PAGE_FRAME_SIZE;
        Memory::MemoryMapFlags flags = ParseFlags((np::Syscall::MemoryMapFlags)regs.arg2, isUserThread, true);

        currentThread->Parent()->VMM()->AddRange({ regs.arg0, regs.arg1, flags }, true);

        regs.arg0 = mapBase;
        regs.arg1 = mapLength;
    }

    void UnmapMemory(SyscallRegisters& regs)
    { regs.id = 1; }
    
    void ModifyMemoryFlags(SyscallRegisters& regs)
    { regs.id = 1; }
}