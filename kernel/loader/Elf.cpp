#include <LoaderPrivate.hpp>
#include <Vm.hpp>
#include "ElfSpec.hpp"

namespace Npk::Private
{
    static NpkStatus LoadElf64(VmSpace& space, uintptr_t loadBase, void* data, 
        size_t dataLen)
    {
 
    }

    static NpkStatus LoadElf32(VmSpace& space, uintptr_t loadBase, void* data,
        size_t dataLen)
    {
        (void)space;
        (void)loadBase;
        (void)data;
        (void)dataLen;

        return NpkStatus::Unsupported;
    }

    static NpkStatus GetFileObjectSize(size_t* size, NsObject& source)
    {
        NPK_UNREACHABLE();
    }

    NpkStatus LoadElf(VmSpace& space, uintptr_t loadBase, NsObject& source)
    {
        Elf_Char ELFCLASS_CURRENT = ELFCLASSNONE;
        if constexpr (sizeof(void*) == 4)
            ELFCLASS_CURRENT = ELFCLASS32;
        else if constexpr (sizeof(void*) == 8)
            ELFCLASS_CURRENT = ELFCLASS64;

        constexpr Elf_Char ELFDATA_CURRENT = 
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            ELFDATA2LSB;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            ELFDATA2MSB;
#else
            ELFDATANONE;
#endif

        size_t fileSize;
        auto result = GetFileObjectSize(&fileSize, source);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Trace);
            return result;
        }

        VmSource* src;
        result = GetFileObjectVmSource(&src, source);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Trace);
            return result;
        }

        VmRange* range;
        result = SpaceAttach(&range, space, 1, fileSize, src, 0, {});
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Trace);
            return result;
        }

        //TODO: dont access struct members directly, use accessor funcs
        void* fileData = reinterpret_cast<void*>(range->base);

        auto* ehdr = static_cast<Elf_Ehdr*>(fileData);
        if (sl::MemCompare(ehdr->e_ident, ExpectedMagic, 4) != 0)
            return NpkStatus::InvalidArg;
        if (ehdr->e_version != EV_CURRENT)
            return NpkStatus::InvalidArg;
        if ((ehdr->e_type != ET_DYN) && (ehdr->e_type != ET_EXEC))
            return NpkStatus::InvalidArg;
        if (ehdr->e_ident[EI_DATA] != ELFDATA_CURRENT)
            return NpkStatus::InvalidArg;
        if (ehdr->e_ident[EI_CLASS] != ELFCLASS_CURRENT)
            return NpkStatus::InvalidArg;

        if (ehdr->e_ident[EI_CLASS] == ELFCLASS32)
            result = LoadElf32(space, loadBase, fileData, fileSize);
        else if (ehdr->e_ident[EI_CLASS] == ELFCLASS64)
            result = LoadElf64(space, loadBase, fileData, fileSize);
        else
            result = NpkStatus::Unsupported;

        auto postResult = SpaceDetach(space, range, true);
        if (postResult != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(postResult, LogLevel::Error);

        return result;
    }
}
