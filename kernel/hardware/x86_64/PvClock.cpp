#include <Core.hpp>
#include <Vm.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <lib/Mmio.hpp>
#include <lib/Memory.hpp>

namespace Npk
{
    static uintptr_t pvClockBase;
    CPU_LOCAL(PvSystemTime*, localPvClock);

    bool TryInitPvClocks(uintptr_t& virtBase)
    {
        if (ReadConfigUint("npk.x86.ignore_pv_clock", false))
            return false;
        if (!CpuHasFeature(CpuFeature::PvClock))
            return false;

        pvClockBase = virtBase;

        auto flags = VmFlag::Mmio | VmFlag::Write;
        for (size_t i = 0; i < MySystemDomain().smpControls.Size(); i++)
        {
            auto page = AllocPage(true);
            NPK_ASSERT(page != nullptr);
            auto paddr = LookupPagePaddr(page);

            auto result = SetKernelMap(virtBase, paddr, flags);
            NPK_ASSERT(result == NpkStatus::Success);

            sl::MmioRegister<Paddr> store = virtBase;
            store.Write(paddr);

            virtBase += PageSize();
        }

        Log("PvClock available, IO at 0x%tx", LogLevel::Info,
            pvClockBase);

        return true;
    }

    bool LocalPvClockInit()
    {
        constexpr uint64_t EnableBit = 1 << 0;

        if (!CpuHasFeature(CpuFeature::PvClock))
            return false;
        if (pvClockBase == 0)
            return false;

        const auto vaddr = pvClockBase + (MyRelativeCoreId() << PfnShift());
        sl::MmioRegister<Paddr> store = vaddr;
        const auto paddr = store.Read();
        store.Write(0);

        WriteMsr(Msr::PvSystemTime, paddr | EnableBit);
        localPvClock = reinterpret_cast<PvSystemTime*>(vaddr);

        return true;
    }

    bool PvClockAvailable()
    {
        return *localPvClock != nullptr;
    }

    uint32_t PvClockVersion()
    {
        sl::MmioRegister<uint32_t> reg = &(*localPvClock)->version;
        const auto ver = reg.Read();

        return ver;
    }

    PvSystemTime ReadPvClock()
    {
        return **localPvClock;
    }

    uint64_t PvClockFrequency(const PvSystemTime& clock)
    {
        if (clock.tscToSystemMul == 0)
            return 0;

        auto freq = static_cast<uint64_t>(sl::Nanos) << 32;
        freq /= clock.tscToSystemMul;

        if (clock.tscShift < 0)
            freq <<= -clock.tscShift;
        else
            freq >>= clock.tscShift;

        return freq;
    }

    sl::Opt<uint64_t> PvClockTscFrequency()
    {
        if (!PvClockAvailable())
            return {};

        const PvSystemTime clock = ReadPvClock();
        const uint64_t freq = PvClockFrequency(clock);
        if (freq == 0)
            return {};

        return freq;
    }
}
