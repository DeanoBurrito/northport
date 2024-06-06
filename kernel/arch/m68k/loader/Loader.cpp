#include "Loader.h"
#include "Memory.h"
#include "Util.h"
#include <formats/Elf.h>

//bit of assembly in the global scope - you love to see it
asm("\
.section .rodata \n\
.balign 0x10 \n\
KERNEL_BLOB_BEGIN: \n\
    .incbin \"build/kernel.elf\" \n\
KERNEL_BLOB_END: \n\
.previous \n\
");

namespace Npl
{
    bool LoadKernel()
    {
        sl::CNativePtr blob(KERNEL_BLOB_BEGIN);
        auto ehdr = blob.As<sl::Elf_Ehdr>();
        //TODO: verify elf header: magic, class, data, version, machine type

        if (ehdr->e_phentsize != sizeof(sl::Elf_Phdr))
            return false;
        const size_t phdrCount = ehdr->e_phnum;
        auto phdrs = blob.Offset(ehdr->e_phoff).As<sl::Elf_Phdr>();

        for (size_t i = 0; i < phdrCount; i++)
        {
            void* segment = MapMemory(phdrs[i].p_memsz, phdrs[i].p_vaddr);
            if (segment == nullptr)
                return false;

            sl::memcopy(blob.Offset(phdrs[i].p_offset).ptr, segment, phdrs[i].p_filesz);
            const size_t zeroes = phdrs[i].p_memsz - phdrs[i].p_filesz;
            if (zeroes != 0)
                sl::memset(sl::NativePtr(segment).Offset(phdrs[i].p_filesz).ptr, 0, zeroes);
        }

        //TODO: support relocating the kernel

        return true;
    }

    void ExecuteKernel()
    {
        sl::CNativePtr blob(KERNEL_BLOB_BEGIN);
        auto ehdr = blob.As<sl::Elf_Ehdr>();
    }
}
