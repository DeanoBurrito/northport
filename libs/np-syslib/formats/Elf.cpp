#include <formats/Elf.hpp>

namespace sl
{
    Elf_UnsignedChar ElfCurrentClass()
    {
        if constexpr (sizeof(void*) == 4)
            return ELFCLASS32;
        else if constexpr (sizeof(void*) == 8)
            return ELFCLASS64;
        return ELFCLASSNONE;
    }

    Elf_UnsignedChar ElfCurrentData()
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ELFDATA2LSB;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return ELFDATA2MSB;
#endif
        return ELFDATANONE;
    }

    Elf_Half ElfCurrentMachine()
    {
#if defined(__x86_64__)
        return EM_X86_64;
#elif defined(__riscv__)
        return EM_RISCV;
#elif defined(__m68k__)
        return EM_68K;
#else
        return EM_NONE;
#endif
    }

    Elf_UnsignedChar ElfCurrentVersion()
    {
        return EV_CURRENT;
    }

    ComputedRelocation ComputeRuntimeRelocation(Elf_Word type, uintptr_t a, 
        uintptr_t b, uintptr_t p, uintptr_t s, uintptr_t v, uintptr_t z)
    {
        //some archs may not use some of these, which is fine, but we mark them
        //as unused to suppress compiler warnings.
        (void)a;
        (void)b;
        (void)p;
        (void)s;
        (void)v;
        (void)z;

        switch (type)
        {
#ifdef __x86_64__
        case R_X86_64_64:           return { .value = a + s, .length = 8 };
        case R_X86_64_32:           return { .value = a + s, .length = 4 };
        case R_X86_64_RELATIVE:     return { .value = b + a, .length = sizeof(void*) };
        case R_X86_64_JUMP_SLOT:    return { .value = s, .length = sizeof(void*) };
        case R_X86_64_GLOB_DAT:     return { .value = s, .length = sizeof(void*) };
#else
    #error "Unsupported architecture"
#endif
        default:
            SL_UNREACHABLE();
        }
    }
}
