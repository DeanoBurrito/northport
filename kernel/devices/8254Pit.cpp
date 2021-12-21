#include <devices/8254Pit.h>
#include <devices/IoApic.h>
#include <Platform.h>
#include <Cpu.h>

namespace Kernel::Devices
{
    uint32_t pitPinNumber;
    void InitPit(uint8_t destApicId, uint8_t vectorNum)
    {
        //bit 0: use bcd encoding or not, bits 1-3: mode, bits 4-5: access mode, bits 6-7: channel
        //here we set channel 0 to receive a 16 bit binary value, and run in mode 2 (rate generator)
        CPU::PortWrite8(PORT_PIT_COMMAND, 0b00'11'010'0);
        //low and high bytes of '1193', giving us a 1000.151Mz +/- 1% (of course this varies depending on hardware)
        CPU::PortWrite8(PORT_PIT_DATA, 0xA9);
        CPU::PortWrite8(PORT_PIT_DATA, 0x04);

        //setup ioapic mapping
        auto overrideDetails = IoApic::TranslateToGsi(0); //old school devices uses hardcoded irq number 0
        overrideDetails.gsiNum = vectorNum;
        IoApic::Global(overrideDetails.gsiNum)->WriteRedirect(destApicId, overrideDetails);
        pitPinNumber = overrideDetails.irqNum;
        
        SetPitMasked(true);
    }

    volatile uint64_t pitTicks;
    void SetPitMasked(bool masked)
    {
        //NOTE: this could breakdown if we have more than 1 ioapic
        IoApic::Global()->SetPinMask(pitPinNumber, masked);
        if (masked)
            pitTicks = 0;
    }

    uint64_t GetPitTicks()
    { return pitTicks; }

    void PitHandleIrq()
    { pitTicks++; }
}
