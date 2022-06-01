#include <syscalls/Dispatch.h>
#include <filesystem/Vfs.h>
#include <memory/VirtualMemory.h>
#include <scheduling/Thread.h>
#include <SyscallEnums.h>
#include <SyscallStructs.h>
#include <Maths.h>

namespace Kernel::Syscalls
{
    void GetFileInfo(SyscallRegisters& regs)
    {
        using namespace Filesystem;

        if (!Memory::VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)np::Syscall::FileError::InvalidBufferRange;
            return;
        }

        CPU::AllowSma(true);
        sl::String filename(sl::NativePtr(regs.arg0).As<const char>());
        CPU::AllowSma(false);
        auto maybeFile =  VFS::Global()->FindNode(filename);
        if (!maybeFile)
        {
            regs.id = (uint64_t)np::Syscall::FileError::FileNotFound;
            return;
        }

        VfsNode* file = *maybeFile;
        using np::Syscall::FileInfo;
        auto maybeRange = Memory::VMM::Current()->AllocRange(sizeof(FileInfo), true, Memory::MemoryMapFlags::UserAccessible | Memory::MemoryMapFlags::AllowWrites);
        if (maybeRange.base == 0)
        {
            regs.id = (uint64_t)np::Syscall::FileError::InvalidBufferRange;
            return;
        }

        FileInfo* userCopy = sl::NativePtr(maybeRange.base).As<FileInfo>();
        regs.arg3 = (uint64_t)userCopy;

        //populate user's copy of the file details
        CPU::AllowSma(true);
        userCopy->fileSize = file->Details().filesize;
        CPU::AllowSma(false);
        
        //return success code
        regs.id = np::Syscall::SyscallSuccess;
    }

    void OpenFile(SyscallRegisters& regs)
    {
        using namespace Filesystem;
        if (!Memory::VMM::Current()->RangeExists({ regs.arg0, PAGE_FRAME_SIZE }))
        {
            regs.id = (uint64_t)np::Syscall::FileError::InvalidBufferRange;
            return;
        }
        
        CPU::AllowSma(true);
        sl::String filename(sl::NativePtr(regs.arg0).As<const char>());
        auto maybeFile =  VFS::Global()->FindNode(filename);
        CPU::AllowSma(false);
        if (!maybeFile)
        {
            regs.id = (uint64_t)np::Syscall::FileError::FileNotFound;
            return;
        }

        VfsNode* file = *maybeFile;
        //allocate a resource handle for the file, and store the node it points to
        Scheduling::ThreadGroup* threadGroup = Scheduling::ThreadGroup::Current();
        auto maybeRid = threadGroup->AttachResource(Scheduling::ThreadResourceType::FileHandle, file);
        if (!maybeRid)
        {
            regs.id = (uint64_t)np::Syscall::FileError::NoResourceId;
            return;
        }
        
        regs.id = np::Syscall::SyscallSuccess;
        regs.arg0 = *maybeRid;
    }

    void CloseFile(SyscallRegisters& regs)
    {
        using namespace Filesystem;

        Scheduling::ThreadGroup* threadGroup = Scheduling::ThreadGroup::Current();
        threadGroup->DetachResource(regs.arg0);

        regs.id = np::Syscall::SyscallSuccess;
    }

    void ReadFromFile(SyscallRegisters& regs)
    {
        using namespace Filesystem;

        //check we have a valid resource handle, and that it's a file
        Scheduling::ThreadGroup* threadGroup = Scheduling::ThreadGroup::Current();
        auto maybeResource = threadGroup->GetResource(regs.arg0);
        if (!maybeResource)
        {
            regs.id = (uint64_t)np::Syscall::FileError::FileNotFound;
            return;
        }
        VfsNode* file = maybeResource.Value()->res.As<VfsNode>();

        //check with the vmm that the buffer region we're reading from is actually mapped.
        if (!threadGroup->VMM()->RangeExists({ regs.arg2 + (regs.arg1 >> 32), regs.arg3 }))
        {
            regs.id = (uint64_t)np::Syscall::FileError::InvalidBufferRange;
            return;
        }

        //work out how much we can actually read from the file
        const size_t readEnd = sl::min<size_t>(file->Details().filesize, (uint32_t)regs.arg1 + regs.arg3);
        const size_t readLength = readEnd - (uint32_t)regs.arg1;

        //read from the file into the buffer, and return it
        CPU::AllowSma(true);
        regs.arg0 = file->Read((uint32_t)regs.arg1, sl::NativePtr(regs.arg2).As<uint8_t>(), regs.arg1 >> 32, readLength);
        CPU::AllowSma(false);
    }

    void WriteToFile(SyscallRegisters& regs)
    {

    }
}
