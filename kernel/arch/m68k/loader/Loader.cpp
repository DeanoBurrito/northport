#include "Loader.h"
#include "Memory.h"
#include "Util.h"
#include <formats/Elf.h>
#include <interfaces/loader/Limine.h>

//bit of assembly in the global scope - you love to see it
asm("\
.global KERNEL_BLOB_BEGIN \n\
.global KERNEL_BLOB_END \n\
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

    static bool IsKernelValid()
    {
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

        return true;
    }

    static void SetKernelSlide()
    {
        kernelSlide = 0; //TODO: KASLR
        NPL_LOG("Setting kernel slide: %u, effective load window 0x%x->0x%x\r\n", 
            kernelSlide, kernelBase + kernelSlide, kernelTop + kernelSlide);
    }

    static bool DoRelocation(bool isRela, const sl::Elf_Rela* r, const sl::Elf_Sym* symTable)
    {
        const auto type = ELF_R_TYPE(r->r_info);
        const uintptr_t b = kernelSlide;
        const uintptr_t p = b + r->r_offset;
        const uintptr_t s = symTable[ELF_R_SYM(r->r_info)].st_value;

        uintptr_t a = 0;
        if (isRela)
            a = r->r_addend;
        else
        {
            const auto op = sl::ComputeRelocation(type, 0, b, s, p);
            if (op.length == 0)
            {
                NPL_LOG("Relocation failed: unknown type %u\r\n", type);
                return false;
            }
            sl::memcopy(reinterpret_cast<void*>(p), &a, op.length);
        }

        const auto op = sl::ComputeRelocation(type, a, b, s, p);
        if (op.usedSymbol && s == 0)
        {
            NPL_LOG("Relocation failed: tried to reference external symbol.\r\n");
            return false;
        }
        if (op.length == 0)
        {
            NPL_LOG("Relocation failed: unknown type %u\r\n", type);
            return false;
        }

        sl::memcopy(&op.value, reinterpret_cast<void*>(p), op.length);
        return true;
    }

    static bool DoKernelRelocations(sl::CNativePtr blob)
    { 
        auto ehdr = blob.As<sl::Elf_Ehdr>();
        const sl::Elf_Dyn* dyn = [=]() -> const sl::Elf_Dyn*
        {
            auto phdrs = blob.As<sl::Elf_Phdr>(ehdr->e_phoff);
            for (size_t i = 0; i < ehdr->e_phnum; i++)
            {
                if (phdrs[i].p_type != sl::PT_DYNAMIC)
                    continue;
                if (phdrs[i].p_filesz == 0)
                    return nullptr;
                return blob.Offset(phdrs[i].p_offset).As<const sl::Elf_Dyn>();
            }
            return nullptr;
        }();

        if (dyn == nullptr)
        {
            NPL_LOG("Kernel has no valid dynamic segment.\r\n");
            return true;
        }
        NPL_LOG("Kernel dynamic segment at %p\r\n", dyn);

        const sl::Elf_Sym* symTable;
        const uint8_t* pltRelocs;
        size_t pltRelocSize = 0;
        bool pltUsesRela = false;
        const sl::Elf_Rela* relas;
        const sl::Elf_Rel* rels;
        size_t relaCount = 0;
        size_t relCount = 0;

        for (auto scan = dyn; scan->d_tag != sl::DT_NULL; scan++)
        {
            switch (scan->d_tag)
            {
            case sl::DT_NEEDED: 
                return false;
            case sl::DT_SYMTAB:
                symTable = reinterpret_cast<const sl::Elf_Sym*>(scan->d_ptr + kernelSlide);
                break;
            case sl::DT_JMPREL:
                pltRelocs = reinterpret_cast<const uint8_t*>(scan->d_ptr + kernelSlide);
                break;
            case sl::DT_PLTRELSZ:
                pltRelocSize = scan->d_val;
                break;
            case sl::DT_PLTREL:
                pltUsesRela = (scan->d_val == sl::DT_RELA);
                break;
            case sl::DT_RELA:
                relas = reinterpret_cast<const sl::Elf_Rela*>(scan->d_ptr + kernelSlide);
                break;
            case sl::DT_REL:
                rels = reinterpret_cast<const sl::Elf_Rel*>(scan->d_ptr + kernelSlide);
                break;
            case sl::DT_RELASZ:
                relaCount = scan->d_val / sizeof(sl::Elf_Rela);
                break;
            case sl::DT_RELSZ:
                relCount = scan->d_val / sizeof(sl::Elf_Rel);
                break;
            default:
                continue;
            }
        }

        size_t failedCount = 0;
        for (size_t i = 0; i < relCount; i++)
        {
            if (!DoRelocation(false, reinterpret_cast<const sl::Elf_Rela*>(&rels[i]), symTable))
                failedCount++;
        }
        for (size_t i = 0; i < relaCount; i++)
        {
            if (!DoRelocation(true, &relas[i], symTable))
                failedCount++;
        }

        size_t pltCount = 0;
        for (size_t off = 0; off < pltRelocSize;)
        {
            auto rela = reinterpret_cast<const sl::Elf_Rela*>(pltRelocs + off);
            if (!DoRelocation(pltUsesRela, rela, symTable))
                failedCount++;

            off += pltUsesRela ? sizeof(sl::Elf_Rela) : sizeof(sl::Elf_Rel);
            pltCount++;
        }

        if (failedCount != 0)
        {
            NPL_LOG("Kernel relocations done: %u failed.\r\n", failedCount);
            return false;
        }

        NPL_LOG("Kernel relocations done: rel=%u, rela=%u, plt=%u\r\n", relCount, relaCount, pltCount);
        return true;
    }

    bool LoadKernel()
    {
        NPL_LOG("Loading kernel ...\r\n");
        if (!IsKernelValid())
        {
            NPL_LOG("Aborting load: kernel image invalid.\r\n");
            return false;
        }

        kernelBase = ~0;
        kernelTop = 0;
        sl::CNativePtr blob(KERNEL_BLOB_BEGIN);
        auto ehdr = blob.As<sl::Elf_Ehdr>();

        //gather some initial info about memory required for the kernel
        const size_t phdrCount = ehdr->e_phnum;
        auto phdrs = blob.Offset(ehdr->e_phoff).As<sl::Elf_Phdr>();
        for (size_t i = 0; i < phdrCount; i++)
        {
            if (phdrs[i].p_type != sl::PT_LOAD || phdrs[i].p_memsz == 0)
                continue;

            if (phdrs[i].p_vaddr < kernelBase)
                kernelBase = phdrs[i].p_vaddr;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > kernelTop)
                kernelTop = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        }
        NPL_LOG("Kernel image requests load window: 0x%x->0x%x\r\n", kernelBase, kernelTop);

        SetKernelSlide();

        kernelPhysBase = []()
        {
            const size_t allocSize = sl::AlignUp(kernelTop, PageSize) - sl::AlignDown(kernelBase, PageSize);
            return AllocPages(allocSize / PageSize, MemoryType::KernelModules);
        }();
        if (kernelPhysBase == 0)
            Panic(PanicReason::LoadAllocFailure);
        NPL_LOG("Kernel physical base: 0x%x\r\n", kernelPhysBase);

        sl::NativePtr slate = MapMemory(kernelTop - kernelBase, kernelBase + kernelSlide, kernelPhysBase);
        for (size_t i = 0; i < phdrCount; i++)
        {
            const sl::Elf_Phdr phdr = phdrs[i];
            if (phdr.p_type != sl::PT_LOAD || phdr.p_memsz == 0)
                continue;

            void* dest = slate.Offset(phdr.p_vaddr - kernelBase).ptr;
            sl::memcopy(blob.Offset(phdr.p_offset).ptr, dest, phdr.p_filesz);
            const size_t zeroes = phdr.p_memsz - phdr.p_filesz;
            if (zeroes != 0)
                sl::memset(sl::NativePtr(dest).Offset(phdr.p_filesz).ptr, 0, zeroes);

            NPL_LOG("Segment %u loaded at %p, 0x%x bytes, 0x%x zeroes appended.\r\n", i,
                dest, phdr.p_filesz, zeroes);
        }
        
        return DoKernelRelocations(blob);
    }

    void ExecuteKernel()
    {
        NPL_LOG("Loader finished, jumping to kernel. Bye!\r\n");

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
        if (current == nullptr)
            scan = kernelBase + kernelSlide;
        scan = sl::AlignUp(scan.raw + 1, 8);

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
