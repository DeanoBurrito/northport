#include <devices/PciBridge.h>
#include <acpi/AcpiTables.h>
#include <drivers/DriverManager.h>
#include <Platform.h>
#include <Memory.h>
#include <Cpu.h>
#include <Log.h>

namespace Kernel::Devices
{
    uint32_t PciAddress::ReadReg(size_t index)
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            return sl::MemRead<uint32_t>(EnsureHigherHalfAddr(addr + (index * 4)));
        else
        {
            CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PCI_ENABLE_CONFIG_CYCLE + (index * 4)));
            return CPU::PortRead32(PORT_PCI_CONFIG_DATA);
        }
    }

    void PciAddress::WriteReg(size_t index, uint32_t data)
    {
        if (addr >> 32 != 0xFFFF'FFFF)
            sl::MemWrite(EnsureHigherHalfAddr(addr + (index * 4)), data);
        else
        {
            CPU::PortWrite32(PORT_PCI_CONFIG_ADDRESS, (uint32_t)addr | (PCI_ENABLE_CONFIG_CYCLE + (index * 4)));
            CPU::PortWrite32(PORT_PCI_CONFIG_DATA, data);
        }
    }

    void PciFunction::Init()
    {
        uint32_t regs[16];
        for (size_t i = 0; i < 16; i++)
            regs[i] = addr.ReadReg(i);
        
        //populate header ids
        header.ids.vendor = regs[0] & 0xFFFF;
        header.ids.device = regs[0] >> 16;
        header.ids.deviceClass = regs[2] >> 24;
        header.ids.deviceSubclass = (regs[2] >> 16) & 0xFF;
        header.revision = regs[2] & 0xFF;
        header.progIf = (regs[2] >> 8) & 0xFF;

        //get capabilities offset
        uint16_t statusReg = regs[1] >> 16;
        if (statusReg & (1 << 4))
            header.capabilitiesOffset = regs[0xD] & 0xFF;
        else
            header.capabilitiesOffset = 0;
        header.interruptVector = 0;

        //rest of the data is conditional, determine how to parse rest of the header
        if ((regs[3] >> 16 & 0x7F) != 0)
        {
            //TODO: for now we'll just handle type 0 headers. Need to implement PCI-to-PCI bridges at some point.
            Logf("PCI func with unknown header type: 0x%x", LogSeverity::Error, (regs[3] >> 16) & 0xFF);
            return;
        }

        header.ids.subsystem = regs[0xB] >> 16;
        header.ids.subsystemVendor = regs[0xB] & 0xFFFF;
        
        //map and populate bar entries (but not allocate!)
        size_t barCount = 0;
        constexpr size_t BarBase = 4;
        constexpr size_t BarMaxCount = 6;
        for (size_t i = BarBase; i < BarBase + BarMaxCount; i++)
        {   
            PciBar* bar = &header.bars[i - 4];
            bar->address = bar->size = 0;
            if (regs[i] == 0)
                continue;

            barCount++;
            if ((regs[i] & 1) == 0)
            {
                //memory space BAR
                bar->isMemory = true;
                bar->is64BitWide = (regs[i] & 0b110) == 0x2;
                bar->isPrefetchable = regs[i] & 1 << 3;
                bar->address = regs[i] & ~0b1111;

                addr.WriteReg(i, 0xFFFF'FFFF);
                uint32_t readback = addr.ReadReg(i);
                bar->size = ~(readback & ~0b1111) + 1;
                addr.WriteReg(i, regs[i]);
            }
            else
            {
                //io space BAR
                bar->isMemory = bar->is64BitWide = bar->isPrefetchable = false;
                bar->address = regs[i] & ~0b11;

                addr.WriteReg(i, 0xFFFF'FFFF);
                uint32_t readback = addr.ReadReg(i);
                bar->size = ~(readback & ~0b11) + 1;
                addr.WriteReg(i, regs[i]);
            }
        }
        
        Logf("PCI endpoint [bus %u, dev %u, func %u]: vendor=0x%x, device=0x%x, class=0x%x, subclass=0x%x, BARs=%u.", LogSeverity::Verbose, parent->parent->id, parent->id, id, header.ids.vendor, header.ids.device, header.ids.deviceClass, header.ids.deviceSubclass, barCount);
    }

    void PciDevice::Init()
    {
        uint32_t reg3 = addr.ReadReg(3);
        size_t maxFunctions = ((reg3 >> 16) & 0x80) != 0 ? 8 : 1; //if multi function, scan each function, otherwise just check first
        
        for (size_t i = 0; i < maxFunctions; i++)
        {
            PciAddress funcAddr = addr.IsLegacy() ? PciAddress::CreateLegacy(parent->id, id, i, 0) : PciAddress::CreateEcam(parent->parent->baseAddress, parent->id, id, i, 0);
            uint32_t reg0 = funcAddr.ReadReg(0);

            if ((reg0 & 0xFFFF) == VENDOR_ID_NONEXISTENT)
                continue;


            functionBitmap |= 1 << i;
            PciFunction function(i, this);
            function.addr = funcAddr;
            function.Init();

//yeah yeah, it works and make the code below it cleaner
#define MAKE_FIT(x) (uint8_t)(function.header.ids.x)
            uint8_t machName[] = { MAKE_FIT(device), MAKE_FIT(device >> 8), MAKE_FIT(vendor), MAKE_FIT(vendor >> 8) };
#undef MAKE_FIT
            functions.PushBack(sl::Move(function));
            
            //check if there's a driver available for the function, create an instance if required
            using namespace Drivers;
            sl::Opt<DriverManifest*> driver = DriverManager::Global()->FindDriver(DriverSubsystem::PCI, DriverMachineName {.length = 4, .name = machName});

            if (driver) //queue driver for loading after pci tree has been fully populated
                PciBridge::Global()->pendingDriverLoads->EmplaceBack(&functions.Back(), driver.Value());
        }
    }

    sl::Opt<const PciFunction*> PciDevice::GetFunction(size_t index) const
    {
        for (auto it = functions.Begin(); it != functions.End(); ++it)
        {
            if (it->id == index)
                return it;
        }

        return {};
    }
    
    void PciSegmentGroup::Init()
    {
        bool ecamAvail = PciBridge::Global()->EcamAvailable();
        
        //brute-force scan approach, works fine for now
        for (size_t bus = 0; bus < 256; bus++)
        {
            children.PushBack(PciBus(bus, this));
            PciBus* tempBus = &children.Back();
            
            for (size_t dev = 0; dev < 32; dev++)
            {
                PciAddress addr = ecamAvail ? PciAddress::CreateEcam(baseAddress, bus, dev, 0, 0) : PciAddress::CreateLegacy(bus, dev, 0, 0);
                uint32_t reg0 = addr.ReadReg(0);
                
                if ((reg0 & 0xFFFF) == VENDOR_ID_NONEXISTENT)
                    continue; //no d(ev)ice

                PciDevice device(dev, tempBus);
                device.addr = addr;
                device.Init();
                tempBus->devices.PushBack(sl::Move(device));
            }

            if (tempBus->devices.Size() == 0)
            {
                //remove unused bus
                (void)children.PopBack();
            }
        }
    }

    PciBridge globalPciBridge;
    PciBridge* PciBridge::Global()
    { return &globalPciBridge; }

    void PciBridge::Init()
    {
        segments = new sl::Vector<PciSegmentGroup>;
        pendingDriverLoads = new sl::Vector<DelayLoadedPciDriver>();
        
        //determine which config mechanism to use, and setup segments accordingly
        ACPI::MCFG* mcfg = reinterpret_cast<ACPI::MCFG*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MCFG));
#ifdef NORTHPORT_PCI_FORCE_LEGACY_ACCESS
        mcfg = nullptr;
#endif

        if (mcfg != nullptr)
        {
            ecamAvailable = true;

            //discover all pci segment groups
            size_t segmentCount = (mcfg->length - sizeof(ACPI::MCFG)) / sizeof(ACPI::EcamEntry);
            for (size_t i = 0; i < segmentCount; i++)
            {
                ACPI::EcamEntry* entry = &mcfg->entries[i];

                segments->EmplaceBack((size_t)entry->pciSegmentGroup, (NativeUInt)entry->baseAddress);
                Logf("PCIe segment group added: id=0x%lx, baseAddress=0x%lx", LogSeverity::Verbose, entry->pciSegmentGroup, entry->baseAddress);
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

        //load any drivers we detected
        for (auto it = pendingDriverLoads->Begin(); it != pendingDriverLoads->End(); ++it)
        {
            Drivers::DriverInitTagPciFunction initTag(it->function);
            Drivers::DriverManager::Global()->StartDriver(it->manifest, &initTag);
        }

        Logf("PCI bridge has delay loaded %u drivers.", LogSeverity::Verbose, pendingDriverLoads->Size());
        pendingDriverLoads->Clear();
        delete pendingDriverLoads;
        pendingDriverLoads = nullptr;
    }

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

    sl::Opt<const PciFunction*> PciBridge::GetFunction(size_t segment, size_t bus, size_t device, size_t function) const
    {
        sl::Opt<const PciDevice*> maybeDev = GetDevice(segment, bus, device);
        if (!maybeDev)
            return {};

        const PciDevice* devVal = *maybeDev;
        for (auto it = devVal->functions.Begin(); it != devVal->functions.End(); ++it)
        {
            if (it->id == function)
                return it;
        }

        return {};
    }

    sl::Opt<const PciDevice*> PciBridge::FindDevice(uint16_t vendorId, uint16_t deviceId) const
    {
        for (auto segmentIt = segments->Begin(); segmentIt != segments->End(); ++segmentIt)
        {
            for (auto busIt = segmentIt->children.Begin(); busIt != segmentIt->children.End(); ++busIt)
            {
                for (auto deviceIt = busIt->devices.Begin(); deviceIt != busIt->devices.End(); ++deviceIt)
                {   
                    if (deviceIt->functions[0].header.ids.vendor == vendorId && deviceIt->functions[0].header.ids.device == deviceId)
                        return deviceIt;
                }
            }
        }

        return {};
    }
}
