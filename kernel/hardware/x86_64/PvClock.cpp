#include <Core.hpp>
#include <Vm.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <Mmio.hpp>
#include <Memory.hpp>

namespace Npk
{
    struct SL_PACKED(PvSystemTime
    {
        uint32_t version;
        uint32_t reserved0;
        uint64_t tscReference;
        uint64_t systemTime;
        uint32_t tscToSystemMul;
        int8_t tscShift;
        uint8_t flags;
        uint8_t reserved1[2];
    });

    static PvSystemTime* systemTime;

    bool TryInitPvClocks(uintptr_t& virtBase)
    {
        const uint32_t EnableBit = 1 << 0;

        if (!CpuHasFeature(CpuFeature::PvClock))
            return false;

        auto page = AllocPage(true);
        NPK_CHECK(page != nullptr, false);

        const Paddr paddr = LookupPagePaddr(page);
        if (SetKernelMap(virtBase, paddr, VmFlag::Mmio) != VmStatus::Success)
        {
            FreePage(page);
            return false;
        }

        WriteMsr(Msr::PvSystemTime, paddr | EnableBit);
        systemTime = reinterpret_cast<PvSystemTime*>(virtBase);
        virtBase += PageSize();

        Log("PvClock enabled, io at 0x%tx", LogLevel::Info, paddr);
        return true;
    }

    uint64_t ReadPvSystemTime()
    {
        if (systemTime == nullptr)
            return 0;

        PvSystemTime copy;

        sl::MmioRegister<decltype(PvSystemTime::version)> versionReg = 
            &systemTime->version;

        while (true)
        {
            uint32_t version;
            while ((version = versionReg.Read()) & 0b1)
                sl::HintSpinloop();

            sl::MemCopy(&copy, systemTime, sizeof(copy));

            const uint32_t endVersion = versionReg.Read();
            if (version == endVersion)
                break;
        }
        
        //NOTE: __uint128_t is a gcc/clang compiler extension only present
        //on some platforms. PvClock is only available on x86, so there are no
        //portability concerns here.
        __uint128_t time = ReadTsc() - copy.tscReference;
        if (copy.tscShift < 0)
            time >>= -copy.tscShift;
        else
            time <<= copy.tscShift;
        time = (time * copy.tscToSystemMul) >> 32;
        time += copy.systemTime;

        return static_cast<uint64_t>(time);
    }
}
