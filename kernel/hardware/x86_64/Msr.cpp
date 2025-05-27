#include <hardware/x86_64/Msr.hpp>
#include <KernelApi.hpp>

namespace Npk
{
    constexpr const char* MtrrTypeStrs[] = 
    { 
        "uc", "wc", "", "",
        "wt", "wp", "wb", ""
    };

    static void LogMttr(size_t index, bool saving, uint64_t base, uint64_t mask)
    {
        const bool valid = mask & (1 << 11);
        const uint8_t type = base & 0xFF;
        const uint64_t baseAddr = base & ~(uint64_t)0xFFF;
        const uint64_t maskValue = mask & ~(uint64_t)0xFFF;

        Log("%s MTRR %zu: valid=%s, type=%s, base=0x%tx, mask=0x%tx", LogLevel::Trace,
            saving ? "Saving" : "Restoring", 
            index, 
            valid ? "yes" : "no", 
            MtrrTypeStrs[type], 
            baseAddr, 
            maskValue);
    }

    void SaveMtrrs(sl::Span<uint64_t> regs)
    {
        const uint64_t caps = ReadMsr(Msr::MtrrCap);

        const size_t vcount = caps & 0xFF;
        const size_t fixedCount = (caps & 0x100) ? 11 : 0;
        NPK_CHECK((vcount * 2) + fixedCount <= regs.Size(), );

        for (size_t i = 0; i < vcount; i++)
        {
            regs[i * 2] = ReadMsr((Msr)((uint32_t)Msr::MtrrPhysBase + i * 2));
            regs[i * 2 + 1] = ReadMsr((Msr)((uint32_t)Msr::MtrrPhysMask + i * 2));
            LogMttr(i, true, regs[i * 2], regs[i * 2 + 1]);
        }

        if (fixedCount == 0)
            return;

        regs[vcount * 2 + 0] = ReadMsr((Msr)0x250);
        regs[vcount * 2 + 1] = ReadMsr((Msr)0x258);
        regs[vcount * 2 + 2] = ReadMsr((Msr)0x259);
        regs[vcount * 2 + 3] = ReadMsr((Msr)0x268);
        regs[vcount * 2 + 4] = ReadMsr((Msr)0x269);
        regs[vcount * 2 + 5] = ReadMsr((Msr)0x26A);
        regs[vcount * 2 + 6] = ReadMsr((Msr)0x26B);
        regs[vcount * 2 + 7] = ReadMsr((Msr)0x26C);
        regs[vcount * 2 + 8] = ReadMsr((Msr)0x26D);
        regs[vcount * 2 + 9] = ReadMsr((Msr)0x26E);
        regs[vcount * 2 + 10] = ReadMsr((Msr)0x26F);
        for (size_t i = 0 ; i < fixedCount; i++)
            Log("Saving fixed MTRR: 0x%" PRIx64, LogLevel::Trace, regs[vcount * 2 + i]);
    }

    void RestoreMtrrs(sl::Span<uint64_t> regs)
    {
        const uint64_t caps = ReadMsr(Msr::MtrrCap);

        const size_t vcount = caps & 0xFF;
        const size_t fixedCount = (caps & 0x100) ? 11 : 0;
        NPK_CHECK((vcount * 2) + fixedCount <= regs.Size(), );

        for (size_t i = 0; i < vcount; i++)
        {
            LogMttr(i, false, regs[i * 2], regs[i * 2 + 1]);
            WriteMsr((Msr)((uint32_t)Msr::MtrrPhysBase + i * 2), regs[i * 2]);
            WriteMsr((Msr)((uint32_t)Msr::MtrrPhysMask + i * 2), regs[i * 2 + 1]);
        }

        if (fixedCount == 0)
            return;
        for (size_t i = 0; i < fixedCount; i++)
            Log("Restoring fixed MTRR: 0x%" PRIx64, LogLevel::Trace, regs[vcount * 2 + i]);

        WriteMsr((Msr)0x250, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x258, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x259, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x268, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x269, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26A, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26B, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26C, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26D, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26E, regs[vcount * 2 + 0]);
        WriteMsr((Msr)0x26F, regs[vcount * 2 + 0]);
    }
}
