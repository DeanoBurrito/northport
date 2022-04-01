#include <Loader.h>
#include <Log.h>
#include <Memory.h>
#include <elf/Elf64.h>
#include <elf/HeaderParser.h>
#include <filesystem/Vfs.h>
#include <scheduling/Scheduler.h>

namespace Kernel
{
    sl::Opt<size_t> LoadElfFromMemory(sl::NativePtr loadedFile, Scheduling::ThreadFlags threadFlags)
    {   
        sl::Elf64_Ehdr* ehdr = loadedFile.As<sl::Elf64_Ehdr>();
        sl::Elf64HeaderParser elfHeader(loadedFile);
        Scheduling::ThreadGroup* tg = Scheduling::Scheduler::Global()->CreateThreadGroup();
        const sl::Vector<sl::Elf64_Phdr*> phdrs = elfHeader.FindProgramHeaders(sl::PT_LOAD);

        //since we're about to mess with CR3, the scheduler will mess with us if we get pre-empted.
        InterruptLock intLock;

        //since we're going to be accessing lower half addresses, swap to the new processes page map.
        //this is fine since all our buffers are in kernel space (loaded file has higher half addr)
        //TODO: would be nice to clone a section of this table into the current one temporarily so we dont have to have to trash the tlb twice.
        Memory::PageTableManager* prevPageTables = Memory::PageTableManager::Current();
        tg->VMM()->PageTables().MakeActive();

        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            const size_t lowestPage = phdrs[i]->p_vaddr / PAGE_FRAME_SIZE;
            const size_t highestPage = (phdrs[i]->p_vaddr + phdrs[i]->p_memsz) / PAGE_FRAME_SIZE + 1;
            const size_t pagesNeeded = highestPage - lowestPage;

            tg->VMM()->PageTables().MapRange(lowestPage * PAGE_FRAME_SIZE, pagesNeeded, Memory::MemoryMapFlags::AllowWrites);
            sl::memcopy(loadedFile.As<void>(phdrs[i]->p_offset), (void*)phdrs[i]->p_vaddr, phdrs[i]->p_filesz);
            //optionally zero any memory that wasnt copied from the file, but requested in memsz.
            sl::memset(sl::NativePtr(phdrs[i]->p_vaddr).As<void>(phdrs[i]->p_filesz), 0, phdrs[i]->p_memsz - phdrs[i]->p_filesz);
        }

        for (size_t i = 0; i < phdrs.Size(); i++)
        {
            const size_t lowestPage = phdrs[i]->p_vaddr / PAGE_FRAME_SIZE;
            const size_t highestPage = (phdrs[i]->p_vaddr + phdrs[i]->p_memsz) / PAGE_FRAME_SIZE + 1;

            for (size_t page = lowestPage; page < highestPage; page++)
            {
                Memory::MemoryMapFlags requestedFlags = Memory::MemoryMapFlags::UserAccessible;
                if (phdrs[i]->p_flags & sl::PF_W)
                    requestedFlags = sl::EnumSetFlag(requestedFlags, Memory::MemoryMapFlags::AllowWrites);
                if (phdrs[i]->p_flags & sl::PF_X)
                    requestedFlags = sl::EnumSetFlag(requestedFlags, Memory::MemoryMapFlags::AllowExecute);

                tg->VMM()->PageTables().ModifyPageFlags(page * PAGE_FRAME_SIZE, requestedFlags, (size_t)-1);
            }
        }
        
        prevPageTables->MakeActive(); //swap back to previous page tables
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

        return LoadElfFromMemory(buffer, threadFlags);
    }
}
