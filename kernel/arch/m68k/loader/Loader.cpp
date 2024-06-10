#include "Loader.h"
#include "Memory.h"
#include "Util.h"
#include <formats/Elf.h>
#include <boot/Limine.h>

//bit of assembly in the global scope - you love to see it
asm("\
.section .rodata \n\
.balign 0x10 \n\
KERNEL_BLOB_BEGIN: \n\
    .incbin \"build/kernel.elf\" \n\
KERNEL_BLOB_END: \n\
.previous \n\
");

extern "C"
{
    extern void LoaderExit(uintptr_t hhdmBase, uintptr_t jumpTarget); //defined in Entry.S
}

namespace Npl
{
    uintptr_t kernelPhysBase;
    uintptr_t kernelBase;
    uintptr_t kernelTop;
    uintptr_t kernelSlide;

    bool LoadKernel()
    {
        kernelBase = ~0;
        kernelTop = 0;
        kernelSlide = 0;

        sl::CNativePtr blob(KERNEL_BLOB_BEGIN);
        auto ehdr = blob.As<sl::Elf_Ehdr>();

        if (sl::memcmp(&ehdr->e_ident[sl::EI_MAG0], sl::ExpectedMagic, 4) != 0)
            return false;
        if (ehdr->e_machine != sl::EM_68K)
            return false;
        if (ehdr->e_type != sl::ET_EXEC && ehdr->e_type != sl::ET_DYN)
            return false;
        if (ehdr->e_ident[sl::EI_CLASS] != sl::ELFCLASS32)
            return false;
        if (ehdr->e_ident[sl::EI_DATA] != sl::ELFDATA2MSB)
            return false;

        if (ehdr->e_phentsize != sizeof(sl::Elf_Phdr))
            return false;
        const size_t phdrCount = ehdr->e_phnum;
        auto phdrs = blob.Offset(ehdr->e_phoff).As<sl::Elf_Phdr>();

        for (size_t i = 0; i < phdrCount; i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD)
                continue;

            if (phdrs[i].p_vaddr < kernelBase)
                kernelBase = phdrs[i].p_vaddr;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > kernelTop)
                kernelTop = phdrs[i].p_vaddr + phdrs[i].p_memsz;

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
        LoaderExit(HhdmBase, blob.As<sl::Elf32_Ehdr>()->e_entry + kernelSlide);
    }

    void GetKernelBases(uint64_t* phys, uint64_t* virt)
    {
        *phys = kernelPhysBase;
        *virt = kernelBase + kernelSlide;
    }

    LbpRequest* LbpNextRequest(LbpRequest* current)
    {
        constexpr uint64_t CommonMagic[] = { LIMINE_COMMON_MAGIC };

        sl::NativePtr scan = current;
        if (scan.ptr == nullptr)
            scan = sl::AlignUp(kernelBase + kernelSlide, 8);

        for (; scan.raw < kernelTop; scan = scan.raw + 8)
        {
            if (*scan.As<uint64_t>() != CommonMagic[0])
                continue;

            LbpRequest* req = scan.As<LbpRequest>();
            if (req->id[1] != CommonMagic[1])
                continue;

            return req;
        }

        return nullptr;
    }
}
