#include <arch/x86_64/Cpuid.h>
#include <core/Log.h>
#include <stdint.h>

namespace Npk
{
    struct CpuFeatureAccessor
    {
        struct 
        { 
            uint32_t main;
            uint32_t sub;
        } leaf;
        uint8_t index;
        uint8_t shift;
        const char* name;
    };

    constexpr static CpuFeatureAccessor accessors[] =
    {
        { .leaf {1, 0}, .index = 'c', .shift = 31, .name = "vguest" },
        { .leaf {0x8000'0001, 0}, .index = 'd', .shift = 20, .name = "nx" },
        { .leaf {0x8000'0001, 0}, .index = 'd', .shift = 26, .name = "1g-map" },
        { .leaf {1, 0}, .index = 'd', .shift = 13, .name = "global-pages" },
        { .leaf {7, 0}, .index = 'b', .shift = 20, .name = "smap" },
        { .leaf {7, 0}, .index = 'b', .shift = 7, .name = "smep" },
        { .leaf {7, 0}, .index = 'c', .shift = 2, .name = "umip" },
        { .leaf {1, 0}, .index = 'd', .shift = 9, .name = "apic" },
        { .leaf {1, 0}, .index = 'c', .shift = 21, .name = "x2apic" },
        { .leaf {1, 0}, .index = 'd', .shift = 24, .name = "fxsave" },
        { .leaf {1, 0}, .index = 'c', .shift = 26, .name = "xsave" },
        { .leaf {1, 0}, .index = 'd', .shift = 0, .name = "fpu" },
        { .leaf {1, 0}, .index = 'd', .shift = 25, .name = "sse" },
        { .leaf {1, 0}, .index = 'd', .shift = 26, .name = "sse2" },
        { .leaf {6, 0}, .index = 'a', .shift = 2, .name = "arat" },
        { .leaf {1, 0}, .index = 'd', .shift = 4, .name = "tsc" },
        { .leaf {1, 0}, .index = 'c', .shift = 24, .name = "tsc-d" },
        { .leaf {0x8000'0007, 0}, .index = 'd', .shift = 8, .name = "inv-tsc" },
    };

    struct CpuidLeaf
    {
        uint32_t a;
        uint32_t b;
        uint32_t c;
        uint32_t d;

        uint32_t operator[](uint8_t index)
        {
            switch (index)
            {
                case 'a': return a;
                case 'b': return b;
                case 'c': return c;
                case 'd': return d;
            }
            ASSERT_UNREACHABLE();
        }
    };

    static CpuidLeaf& DoCpuid(uint32_t leaf, uint32_t subleaf, CpuidLeaf& data)
    {
        asm volatile("cpuid" : 
            "=a"(data.a), "=b"(data.b), "=c"(data.c), "=d"(data.d) :
            "0"(leaf), "2"(subleaf) :
            "memory");

        return data;
    }

    bool CpuHasFeature(CpuFeature feature)
    {
        const unsigned featIndex = static_cast<unsigned>(feature);
        if (featIndex >= (unsigned)CpuFeature::Count)
            return false;

        //fallback path: try to return correct info even if cache isnt available.
        //`cpuid` is a serializing instruction though, so this can slow things down.
        CpuidLeaf leaf {};
        DoCpuid(accessors[featIndex].leaf.main, accessors[featIndex].leaf.sub, leaf);
        const uint32_t data = leaf[accessors[featIndex].index];
        return data & (1ul << accessors[featIndex].shift);
    }
}
