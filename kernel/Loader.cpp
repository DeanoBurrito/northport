#include <Loader.h>
#include <Log.h>
#include <Memory.h>
#include <elf/Elf64.h>
#include <elf/HeaderParser.h>
#include <filesystem/Vfs.h>
#include <scheduling/Scheduler.h>

namespace Kernel
{
    sl::Opt<size_t> LoadElfFromMemory(sl::BufferView file, Scheduling::ThreadFlags threadFlags)
    {   
        sl::Elf64_Ehdr* ehdr = file.base.As<sl::Elf64_Ehdr>();
        sl::Elf64HeaderParser elfHeader(file.base);
        const sl::Vector<sl::Elf64_Phdr*> phdrs = elfHeader.FindProgramHeaders(sl::PT_LOAD);

        using MFlags = Memory::MemoryMapFlags;
        Scheduling::ThreadGroup* tg = Scheduling::Scheduler::Global()->CreateThreadGroup();
        
        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            if (phdrs[i]->p_align != 0x1000)
            {
                Log("Could not load elf file: program header not 4K aligned.", LogSeverity::Error);
                return {};
            }

            //these are the user flags. We'll be writing to the memory via the hhdm (which is always writable)
            MFlags flags = MFlags::None;
            if (!sl::EnumHasFlag(threadFlags, Scheduling::ThreadFlags::KernelMode))
                flags = flags | MFlags::UserAccessible;
            if (phdrs[i]->p_flags & sl::PF_X)
                flags = flags | MFlags::AllowExecute;
            if (phdrs[i]->p_flags & sl::PF_W)
                flags = flags | MFlags::AllowWrites;

            if (!tg->VMM()->AddRange({ phdrs[i]->p_vaddr, phdrs[i]->p_memsz, flags }, true))
            {
                Logf("Could not load ELF, unable to alloc phdr range: base=0x%lx, len=0x%lx", LogSeverity::Error, phdrs[i]->p_vaddr, phdrs[i]->p_memsz);
                return {};
            }

            if (tg->VMM()->CopyInto({ file.base.raw + phdrs[i]->p_offset, phdrs[i]->p_filesz }, phdrs[i]->p_vaddr) != phdrs[i]->p_filesz)
                Log("Could not copy elf phdr into foreign VMM range, loaded program may not run correctly.", LogSeverity::Error);
            
            if (phdrs[i]->p_memsz > phdrs[i]->p_filesz)
                tg->VMM()->ZeroRange({ phdrs[i]->p_vaddr + phdrs[i]->p_filesz, phdrs[i]->p_memsz - phdrs[i]->p_filesz });
        }

        return Scheduling::Scheduler::Global()->CreateThread(ehdr->e_entry, threadFlags, tg)->Id();
    }

    sl::Opt<size_t> LoadElfFromFile(const sl::String& filename, Scheduling::ThreadFlags threadFlags)
    {
        auto maybeFile = Filesystem::VFS::Global()->FindNode(filename);
        if (!maybeFile)
        {
            Logf("Could not load elf from file: no file named %s", LogSeverity::Error, filename.C_Str());
            return {};
        }

        Filesystem::VfsNode* fileNode = *maybeFile;
        uint8_t* buffer = new uint8_t[fileNode->Details().filesize];
        if (fileNode->Read(0, buffer, 0, fileNode->Details().filesize) != fileNode->Details().filesize)
        {
            Logf("Could not load elf from file: unable to read full file into memory: %s", LogSeverity::Info, filename.C_Str());
            return {};
        }

        sl::Opt<size_t> maybeThread = LoadElfFromMemory({ buffer, fileNode->Details().filesize }, threadFlags);
        if (maybeThread)
        {
            Scheduling::Thread* thread = Scheduling::Scheduler::Global()->GetThread(*maybeThread).Value();
            thread->Name() = "Main thread";
            thread->Parent()->Name() = filename;
        }

        delete[] buffer;
        return maybeThread;
    }
}
