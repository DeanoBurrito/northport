#include <devices/PciBridge.h>
#include <acpi/AcpiTables.h>
#include <Platform.h>
#include <Memory.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices
{
    void PciDevice::Init(size_t devNum)
    {
        id = devNum;

        uint32_t regs[16];
        bool ecamAvail = header.ecamAddress != 0;
        for (size_t i = 0; i < 16; i++)
            regs[i] = ecamAvail ? PciBridge::EcamReadConfig(header.ecamAddress) : PciBridge::LegacyReadConfig(header.legacyAddress);

        header.ids.vendor = regs[0] & 0xFFFF;
        header.ids.device = regs[0] >> 16;
        header.ids.deviceClass = regs[3] >> 24;
        header.ids.deviceSubclass = regs[3] >> 16;
        
        size_t headerType = (regs[3] >> 16) & 0x7F;
        if (headerType == 0)
        {
            header.ids.subsystem = regs[11] >> 16;
            header.ids.subsystemVendor = regs[11] & 0xFFFF;
        }
        else
        { header.ids.subsystem = header.ids.subsystem = 0; }

        header.revision = regs[2] & 0xFF;
        header.progIf = regs[2] >> 8;
        if ((regs[1] >> 20) & 1)
            header.capabilitiesOffset = regs[13] & 0xFF;
        else
            header.capabilitiesOffset = 0;
        header.interruptVector = 0;

        header.functionsBitmap = 0b1;
        if (headerType & 0x80)
        {
            for (size_t i = 0; i < 8; i++)
            {
                uint32_t reg0;
                if (ecamAvail)
                {
                    NativeUInt addr = header.ecamAddress & ~(7 << 12); //clear 3 function bits
                    addr |= i << 12;
                    reg0 = PciBridge::EcamReadConfig(addr);
                }
                else
                {
                    uint32_t addr = header.legacyAddress & ~(7 << 8);
                    addr |= i << 8;
                    reg0 = PciBridge::LegacyReadConfig(addr);
                }

                if ((reg0 & 0xFFFF) != VENDOR_ID_NONEXISTENT)
                    header.functionsBitmap |= (1 << i);
            }
        }

        //TODO: check if a driver is available, if so, allocate bars and mark driver for init
    }
    
    size_t PciDevice::GetId() const
    { return id; }

    const PciDeviceHeader* PciDevice::GetHeader() const
    { return &header; }

    void PciSegmentGroup::Init()
    {
        bool ecamAvail = PciBridge::Global()->EcamAvailable();
        
        //brute-force scan approach TODO: recursive scan 
        for (size_t bus = 0; bus < 256; bus++)
        {
            sl::Vector<PciDevice> devices;
            
            for (size_t dev = 0; dev < 32; dev++)
            {
                NativeUInt ecamAddr = PciBridge::CreateEcamConfigAddr(id, bus, dev, 0, 0);
                uint32_t legacyAddr = PciBridge::CreateLegacyConfigAddr(bus, dev, 0, 0);
                uint32_t reg0 = ecamAvail ? PciBridge::EcamReadConfig(ecamAddr) : PciBridge::LegacyReadConfig(legacyAddr);

                if ((reg0 & 0xFFFF) == VENDOR_ID_NONEXISTENT)
                    continue; //device does not exist

                PciDevice device;
                device.header.ecamAddress = ecamAvail ? ecamAddr : 0;
                device.header.legacyAddress = ecamAvail ? 0 : legacyAddr;
                device.Init(dev);

                devices.PushBack(sl::Move(device));
                Logf("Found PCI device: bus=%u, index=%u, vendor=0x%x, device=0x%x", LogSeverity::Verbose, bus, dev, reg0 & 0xFFFF, reg0 >> 16);
            }

            if (devices.Size() > 0)
            {
                Logf("Adding new populated PCI bus %u (segment %u).", LogSeverity::Verbose, bus, id);
                
                children.EmplaceBack(bus, 0, 0);
                children.Back().devices = sl::Move(devices);
            }
        }
    }

    NativeUInt PciSegmentGroup::GetBaseAddress() const
    { return baseAddress; }

    size_t PciSegmentGroup::GetId() const
    { return id; }

    void PciBridge::LegacyWriteConfig(uint32_t address, uint32_t data)
    {
        CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, address | PCI_ENABLE_CONFIG_CYCLE);
        CPU::PortWrite32(PORT_PCI_CONFIG_DATA, data);
    }

    uint32_t PciBridge::LegacyReadConfig(uint32_t address)
    {
        CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, address | PCI_ENABLE_CONFIG_CYCLE);
        return CPU::PortRead32(PORT_PCI_CONFIG_DATA);
    }

    PciBridge globalPciBridge;
    PciBridge* PciBridge::Global()
    { return &globalPciBridge; }

    uint32_t PciBridge::CreateLegacyConfigAddr(size_t busNumber, size_t deviceNumber, size_t functionNumber, size_t registerOffset)
    {
        return (busNumber << 16) | ((deviceNumber & 0b11111) << 11) | ((functionNumber & 0b111) << 8) | (registerOffset & 0b1111'1100);
    }

    NativeUInt PciBridge::CreateEcamConfigAddr(size_t segment, size_t bus, size_t device, size_t function, size_t reg)
    {
        const NativeUInt addend = bus << 20 | device << 15 | function << 12 | reg & 0xFFC;
        for (size_t i = 0; i < Global()->segments->Size(); i++)
        {
            if (Global()->segments->At(i).GetId() == segment)
                return Global()->segments->At(i).GetBaseAddress() + addend;
        }
        return 0;
    }

    void PciBridge::EcamWriteConfig(NativeUInt address, uint32_t data)
    {
        sl::MemWrite(EnsureHigherHalfAddr(address), data);
    }

    uint32_t PciBridge::EcamReadConfig(NativeUInt address)
    {
        return sl::MemRead<uint32_t>(EnsureHigherHalfAddr(address));
    }

    void PciBridge::Init()
    {
        segments = new sl::Vector<PciSegmentGroup>;
        
        //determine which config mechanism to use, and setup segments accordingly
        ACPI::MCFG* mcfg = reinterpret_cast<ACPI::MCFG*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MCFG));
        if (mcfg != nullptr)
        {
            ecamAvailable = true;

            //discover all pci segment groups
            size_t segmentCount = (mcfg->length - sizeof(ACPI::MCFG)) / sizeof(ACPI::EcamEntry);
            for (size_t i = 0; i < segmentCount; i++)
            {
                ACPI::EcamEntry* entry = &mcfg->entries[i];

                segments->EmplaceBack((size_t)entry->pciSegmentGroup, (NativeUInt)entry->baseAddress, (size_t)entry->pciBusFirst, (size_t)entry->pciBusLast);
                Logf("PCIe segment group added: id=0x%lx, baseAddress=0x%lx, firstBus=0x%x, lastBus=0x%x", LogSeverity::Verbose, entry->pciSegmentGroup, entry->baseAddress, entry->pciBusFirst, entry->pciBusLast);
            }

            Logf("PCIe ecam parsed, %lu segment groups located", LogSeverity::Verbose, segments->Size());
        }
        else
        {
            ecamAvailable = false;
            
            //create segment group 0 to hold the root pci bus
            segments->EmplaceBack(PciSegmentGroup(0, 0));
            Log("PCIe ecam not available, using legacy pci config mechanism.", LogSeverity::Verbose);
        }

        //create a map of the current pci devices
        for (auto it = segments->Begin(); it != segments->End(); ++it)
            it->Init();

        Logf("PCI bridge initialized, ecamSupport=%b", LogSeverity::Info, ecamAvailable);
    }

    bool PciBridge::EcamAvailable() const
    { return ecamAvailable; }

    sl::Opt<const PciSegmentGroup*> PciBridge::GetSegment(size_t id) const
    {
        for (auto it = segments->Begin(); it != segments->End(); ++it)
        {
            if (it->id == id)
                return it;
        }

        return {};
    }

    sl::Opt<const PciBus*> PciBridge::GetBus(size_t segment, size_t bus) const
    {
        sl::Opt<const PciSegmentGroup*> seg = GetSegment(segment);
        if (!seg)
            return {};

        const PciSegmentGroup* segVal = *seg;
        for (auto it = segVal->children.Begin(); it != segVal->children.End(); ++it)
        {
            if (it->id == bus)
                return it;
        }

        return {};
    }

    sl::Opt<const PciDevice*> PciBridge::GetDevice(size_t segment, size_t bus, size_t device) const
    {
        sl::Opt<const PciBus*> maybeBus = GetBus(segment, bus);
        if (!maybeBus)
            return {};
        
        const PciBus* busVal = *maybeBus;
        for (auto it = busVal->devices.Begin(); it != busVal->devices.End(); ++it)
        {
            if (it->id == device)
                return it;
        }

        return {};
    }
}
