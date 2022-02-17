#include <Memory.h>

#ifdef __clang__
//clang compliance: gcc provides built in mem* functions, but clang expects us to supply our own 'builtin functions'.
extern "C"
{
    void memset(void* const ptr, int value, size_t count)
    { sl::memset(ptr, value, count); }

    void memcpy(void* dest, void* src, size_t count)
    { sl::memcopy(src, dest, count); }
}
#endif

namespace sl
{
    void memset(void* const start, uint8_t value, size_t count)
    {
        uint8_t* const si = static_cast<uint8_t* const>(start);
        
        for (size_t i = 0; i < count; i++)
            si[i] = value;
    }

    void memcopy(const void* const source, void* const dest, size_t count)
    {
        const uint8_t* const si = reinterpret_cast<const uint8_t* const>(source);
        uint8_t* di = reinterpret_cast<uint8_t*>(dest);

        for (size_t i = 0; i < count; i++)
            di[i] = si[i];
    }

    void memcopy(const void* const source, size_t sourceOffset, void* const dest, size_t destOffset, size_t count)
    {
        const uint8_t* const si = reinterpret_cast<const uint8_t* const>(source);
        uint8_t* const di = reinterpret_cast<uint8_t* const>(dest);

        for (size_t i = 0; i < count; i++)
            di[destOffset + i] = si[sourceOffset + i];
    }

    int memcmp(const void* const a, const void* const b, size_t count)
    {
        const uint8_t* const ai = reinterpret_cast<const uint8_t* const>(a);
        const uint8_t* const bi = reinterpret_cast<const uint8_t* const>(b);

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
        const uint8_t* ai = reinterpret_cast<const uint8_t* const>(a);
        const uint8_t* bi = reinterpret_cast<const uint8_t* const>(b);
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

    size_t memfirst(const void* const buff, uint8_t target, size_t upperLimit)
    {
        return memfirst(buff, 0, target, upperLimit);
    }

    size_t memfirst(const void* const buff, size_t offset, uint8_t target, size_t upperLimit)
    {
        const uint8_t* const start = static_cast<const uint8_t* const>(buff);

        for (size_t i = offset; i < upperLimit || upperLimit == 0; i++)
        {
            if (start[i] == target)
                return i;
        }
        return (size_t)-1;
    }
}
