#include <syscalls/Dispatch.h>
#include <filesystem/Vfs.h>
#include <memory/VirtualMemory.h>
#include <scheduling/Thread.h>
#include <SyscallEnums.h>
#include <SyscallStructs.h>

namespace Kernel::Syscalls
{
    void GetFileInfo(SyscallRegisters& regs)
    {
        using namespace Filesystem;

        //TODO: check that regs.arg0 is actually a valid region using the vmm (something like IsValid(base, length))
        sl::String filename(sl::NativePtr(regs.arg0).As<const char>());

        auto maybeFile =  VFS::Global()->FindNode(filename);
        if (!maybeFile)
        {
            regs.id = (uint64_t)np::Syscall::FileError::FileNotFound;
            return;
        }

        VfsNode* file = *maybeFile;
        using np::Syscall::FileInfo;
        FileInfo* userCopy = Memory::VMM::Current()->AllocateRange(sizeof(FileInfo), Memory::MemoryMapFlags::UserAccessible).As<FileInfo>();

        //populate user's copy of the file details
        userCopy->fileSize = file->Details().filesize;
        
        //return success code
        regs.id = np::Syscall::SyscallSuccess;
    }

    void OpenFile(SyscallRegisters& regs)
    {
        using namespace Filesystem;
        
        //TODO: check that regs.arg0 is actually a valid region using the vmm (something like IsValid(base, length))
        sl::String filename(sl::NativePtr(regs.arg0).As<const char>());

        auto maybeFile =  VFS::Global()->FindNode(filename);
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
}
