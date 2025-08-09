#include <hardware/x86_64/Hpet.hpp>
#include <AcpiTypes.hpp>
#include <CoreApi.hpp>
#include <Mmio.h>
#include <UnitConverter.h>

namespace Npk
{
    enum class HpetReg
    {
        Capabilities = 0,
        Config = 0x10,
        IntrStatus = 0x20,
        MainCounter = 0xF0,
        Timer0Config = 0x100,
        Timer0Value = 0x108,
        Timer0IntrRouting = 0x110,
    };

    static bool hpetAvailable = false;
    static bool hpetIs64Bit;
    static sl::MmioRegisters<HpetReg, uint64_t> hpetRegs;
    static uint64_t hpetFrequency;

    bool InitHpet(uintptr_t& virtBase)
    {
        if (ReadConfigUint("npk.x86.ignore_hpet", false))
            return false;

        auto maybeHpet = GetAcpiTable(SigHpet);
        if (!maybeHpet.HasValue())
            return false;
        auto hpet = static_cast<Hpet*>(*maybeHpet);

        //the spec *implies* the registers live in memory space, but its never
        //stated as such, so just assert it to be safe.
        NPK_CHECK(hpet->baseAddress.type == AcpiAddrSpace::Memory, false);

        auto mapped = ArchAddMap(MyKernelMap(), virtBase, 
            hpet->baseAddress.address, MmuFlag::Write | MmuFlag::Mmio);
        if (mapped != MmuError::Success)
            return false;

        //ensure main counter is running, we dont care about any other state
        //since we're just reading the main counter.
        hpetRegs.Write(HpetReg::Config, 0b1);

        const uint64_t caps = hpetRegs.Read(HpetReg::Capabilities);
        hpetIs64Bit = caps & (1 << 13);
        const size_t timers = 1 + ((caps >> 8) & 0xF);
        hpetFrequency = sl::Femtos / ((caps >> 32) & 0xFFFF'FFFF);
        
        const auto conv = sl::ConvertUnits(hpetFrequency, sl::UnitBase::Decimal);
        Log("HPET available: counter=%s, timers=%zu, frequency=%zu.%zu %sHz",
            LogLevel::Info, hpetIs64Bit ? "64-bit" : "32-bit", timers,
            conv.major, conv.minor, conv.prefix);

        hpetAvailable = true;
        return true;
    }

    bool HpetAvailable()
    {
        return hpetAvailable;
    }

    bool HpetIs64Bit()
    {
        return hpetAvailable && hpetIs64Bit;
    }

    uint64_t HpetRead()
    {
        if (!hpetAvailable)
            return 0;

        return hpetRegs.Read(HpetReg::MainCounter);
    }

    uint64_t HpetFrequency()
    {
        if (!hpetAvailable)
            return 0;
        return hpetFrequency;
    }
}
