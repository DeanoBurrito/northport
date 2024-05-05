#include <uacpi_stdlib.h>
#include <Memory.h>

extern "C"
{
    void* uacpi_memmove(void* dest, const void* src, size_t count)
    { return memmove(dest, src, count); }

    void* uacpi_memcpy(void* dest, const void* src, size_t count)
    { return memcpy(dest, src, count); }

    void* uacpi_memset(void* dest, int ch, size_t count)
    { return memset(dest, ch, count); }

    int uacpi_memcmp(const void* lhs, const void* rhs, size_t count)
    { return sl::memcmp(lhs, rhs, count); }

    int uacpi_strncmp(const char* lhs, const char* rhs, size_t count)
    {
        for (size_t i = 0; i < count && lhs[i] != 0 && rhs[i] != 0; i++)
        {
            if (lhs[i] == rhs[i])
                continue;
            return lhs[i] < rhs[i] ? -1 : 1;
        }

        return 0;
    }

    int uacpi_strcmp(const char* lhs, const char* rhs)
    { return uacpi_strncmp(lhs, rhs, -1ul); }

    size_t uacpi_strnlen(const char* str, size_t strsz)
    { 
        if (str == nullptr)
            return 0;
        for (size_t i = 0; i < strsz; i++)
        {
            if (str[i] != 0)
                continue;
            return i;
        }

        return strsz;
    }

    size_t uacpi_strlen(const char* str)
    { return uacpi_strnlen(str, -1ul); }

    int __popcountdi2(int64_t a)
    {
        /* This function was taken from https://github.com/mintsuki/cc-runtime, which is a rip
         * of the LLVM compiler runtime library (different flavour of libgcc).
         * See https://llvm.org/LICENSE.txt for the full license and more info.
         */
        uint64_t x2 = (uint64_t)a;
        x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
        x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
        x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
        uint32_t x = (uint32_t)(x2 + (x2 >> 32));
        x = x + (x >> 16);
        return (x + (x >> 8)) & 0x0000007F;
    }
}
