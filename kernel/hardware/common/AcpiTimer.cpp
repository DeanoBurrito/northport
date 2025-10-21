#include <hardware/common/AcpiTimer.hpp>
#include <AcpiTypes.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <Mmio.hpp>

#ifdef __x86_64__
#include <hardware/x86_64/PortIo.hpp>
#endif

namespace Npk
{
    static bool acpiTimerAvailable = false;
    static bool acpiTimerIs32Bit;
    static bool acpiTimerIsMmio;
    static uintptr_t acpiTimerAddress;

    bool InitAcpiTimer(uintptr_t& virtBase)
    {
        if (ReadConfigUint("npk.ignore_acpi_pm_timer", false))
            return false;

        auto maybeFadt = GetAcpiTable(sl::SigFadt);
        if (!maybeFadt.HasValue())
            return false;

        auto fadt = static_cast<sl::Fadt*>(*maybeFadt);
        if (fadt->flags.Has(sl::FadtFlag::HwReducedAcpi))
            return false;

//yes, I'm doing something a bit dodge here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
        if (fadt->length >= offsetof(sl::Fadt, xPmTimerBlock) 
            && fadt->xPmTimerBlock.address != 0)
        {
            acpiTimerAddress = fadt->xPmTimerBlock.address;
            const auto type = fadt->xPmTimerBlock.type;
            NPK_CHECK(type == sl::AcpiAddrSpace::IO 
                || type == sl::AcpiAddrSpace::Memory, false);
            acpiTimerIsMmio = type == sl::AcpiAddrSpace::Memory;

            if (acpiTimerIsMmio)
            {
                auto mapped = SetKernelMap(virtBase, acpiTimerAddress, VmFlag::Mmio);
                if (mapped != VmStatus::Success)
                    return false;

                acpiTimerAddress = virtBase;
                virtBase += PageSize();
            }
        }
        else if (fadt->length >= offsetof(sl::Fadt, pmTimerBlock) 
            && fadt->pmTimerBlock != 0)
        {
            acpiTimerIsMmio = false;
            acpiTimerAddress = fadt->pmTimerBlock;
        }
        else
            return false;
#pragma GCC diagnostic pop

        acpiTimerAvailable = true;
        Log("ACPI timer available: addr=0x%tx (%s), width=%s", LogLevel::Info, 
            acpiTimerAddress, acpiTimerIsMmio ? "mmio" : "pio", 
            acpiTimerIs32Bit ? "32-bit" : "24-bit");

        return true;
    }
    
    bool AcpiTimerAvailable()
    {
        return acpiTimerAvailable;
    }

    bool AcpiTimerIs32Bit()
    {
        return acpiTimerIs32Bit;
    }

    uint32_t AcpiTimerRead()
    {
        if (!acpiTimerAvailable)
            return 0;

        if (acpiTimerIsMmio)
            return sl::MmioRead32(acpiTimerAddress);

#ifdef __x86_64__
        return In32(static_cast<Port>(acpiTimerAddress));
#else
        NPK_UNREACHABLE();
#endif
    }
}
