#include <Memory.h>

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
        {
            di[destOffset + i] = si[sourceOffset + i];
        }
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
