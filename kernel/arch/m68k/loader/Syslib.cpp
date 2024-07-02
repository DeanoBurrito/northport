#include <Memory.h>
#include <formats/Elf.h>

//Syslib does have a version compiled for kernel use, but that uses the got/plt,
//which I dont want to have in the loader. So the functions we need from syslib
//are implemented here instead, and compiled-in (insteading of being linked-in).
namespace sl
{
    void* memset(void* const start, uint8_t value, size_t count)
    {
        uint8_t* const si = static_cast<uint8_t*>(start);
        
        for (size_t i = 0; i < count; i++)
            si[i] = value;
        return start;
    }

    void* memcopy(const void* const source, void* const dest, size_t count)
    {
        const uint8_t* const si = static_cast<const uint8_t*>(source);
        uint8_t* const di = static_cast<uint8_t*>(dest);

        for (size_t i = 0; i < count; i++)
            di[i] = si[i];
        return dest;
    }

    void* memcopy(const void* const source, size_t sourceOffset, void* const dest, size_t destOffset, size_t count)
    {
        const uint8_t* const si = static_cast<const uint8_t*>(source);
        uint8_t* const di = static_cast<uint8_t*>(dest);

        for (size_t i = 0; i < count; i++)
            di[destOffset + i] = si[sourceOffset + i];
        return dest;
    }

    int memcmp(const void* const a, const void* const b, size_t count)
    {
        const uint8_t* const ai = static_cast<const uint8_t*>(a);
        const uint8_t* const bi = static_cast<const uint8_t*>(b);

        for (size_t i = 0; i < count; i++)
        {
            if (ai[i] > bi[i])
                return 1;
            else if (bi[i] > ai[i])
                return -1;
        }
        return 0;
    }

    int memcmp(const void* const a, size_t offsetA, const void* const b, size_t offsetB, size_t count)
    {
        const uint8_t* ai = static_cast<const uint8_t*>(a);
        const uint8_t* bi = static_cast<const uint8_t*>(b);
        ai += offsetA;
        bi += offsetB;

        for (size_t i = 0; i < count; i++)
        {
            if (ai[i] > bi[i])
                return 1;
            else if (bi[i] > ai[i])
                return -1;
        }
        return 0;
    }

    ComputedReloc ComputeRelocation(Elf_Word type, uintptr_t a, uintptr_t b, uintptr_t s, uintptr_t p)
    {
        switch (type)
        {
        case R_68K_GLOB_DAT: return { .value = s, .length = 4, .usedSymbol = true };
        case R_68K_JMP_SLOT: return { .value = s, .length = 4, .usedSymbol = true };
        case R_68K_RELATIVE: return { .value = b + a, .length = 4, .usedSymbol = false };
        }
        return { .value = 0, .length = 0, .usedSymbol = false };
    }
}

extern "C"
{
    void* memcpy(void* dest, const void* src, size_t len)
    { return sl::memcopy(src, dest, len); }

    void* memset(void* dest, int value, size_t len)
    { return sl::memset(dest, value, len); }

    void* memmove(void* dest, const void* src, size_t len)
    {
        uint8_t* di = static_cast<uint8_t*>(dest);
        const uint8_t* si = static_cast<const uint8_t*>(src);

        for (size_t i = len; i > 0; i--)
            di[i - 1] = si[i - 1];

        return dest;
    }
}
