#include <hardware/x86_64/Cpuid.hpp>
#include <Core.hpp>
#include <NanoPrintf.hpp>

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
        { .leaf {1, 0}, .index = 'd', .shift = 16, .name = "pat" },
        { .leaf {0x8000'0008, 0}, .index = 'b', .shift = 21, .name = "invlpgb" },
        { .leaf {1, 0}, .index = 'd', .shift = 12, .name = "mtrr" },
        { .leaf {0x4000'0001, 0}, .index = 'a', .shift = 3, .name = "pv-sysclock" },
        { .leaf {7, 0}, .index = 'b', .shift = 0, .name = "wrfsgsbase" },
        { .leaf {0x8000'0001, 0}, .index = 'd', .shift = 11, .name = "syscall" },
        { .leaf {1, 0}, .index = 'd', .shift = 11, .name = "sysenter" },
        { .leaf {1, 0}, .index = 'd', .shift = 2, .name = "dbg-exts" },
        { .leaf {1, 0}, .index = 'd', .shift = 7, .name = "mce" },
        { .leaf {1, 0}, .index = 'd', .shift = 14, .name = "mca" },
    };

    static_assert(sizeof(accessors) / sizeof(CpuFeatureAccessor) == static_cast<size_t>(CpuFeature::Count));

    CpuidLeaf& DoCpuid(uint32_t leaf, uint32_t subleaf, CpuidLeaf& data)
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

        CpuidLeaf leaf {};
        const uint32_t index = accessors[featIndex].leaf.main;
        if (DoCpuid(index & 0xF000'0000, 0, leaf).a < index)
            return false;

        DoCpuid(accessors[featIndex].leaf.main, accessors[featIndex].leaf.sub, leaf);
        const uint32_t data = leaf[accessors[featIndex].index];
        return data & (1ul << accessors[featIndex].shift);
    }

    void LogCpuFeatures()
    {
        constexpr size_t MsgBufferSize = 64;
        char msgBuff[MsgBufferSize];
        msgBuff[0] = '|';
        msgBuff[1] = ' ';
        size_t msgBuffLen = 2;
        int increment = 1;

        for (size_t i = 0; i < static_cast<size_t>(CpuFeature::Count); i += increment)
        {
            increment = 1;
            const bool hasFeature = CpuHasFeature(static_cast<CpuFeature>(i));
            const size_t idealLen = npf_snprintf(msgBuff + msgBuffLen, MsgBufferSize - msgBuffLen,
                "%s%s=%s", msgBuffLen == 2 ? "" : ", ", accessors[i].name, 
                hasFeature ? "yes" : "no");

            if (idealLen + msgBuffLen >= MsgBufferSize - 1)
            {
                Log("%.*s", LogLevel::Verbose, (int)msgBuffLen, msgBuff);
                msgBuffLen = 2;
                increment = 0;
            }
            else
                msgBuffLen += idealLen;
        }

        if (msgBuffLen != 0)
            Log("%.*s", LogLevel::Verbose, (int)msgBuffLen, msgBuff);
    }
}
