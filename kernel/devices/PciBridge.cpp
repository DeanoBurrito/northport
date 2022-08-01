#include <devices/PciBridge.h>
#include <acpi/AcpiTables.h>
#include <drivers/DriverManager.h>
#include <Configuration.h>
#include <Log.h>

namespace Kernel::Devices
{
    constexpr uint16_t PciVendorNonExistant = 0xFFFF;

    void PciSegmentGroup::TryFindDrivers(PciAddress addr)
    {
        //check if there are any drivers available. We look for a vendor+device id matched
        //driver first, if that doesn't exist we'll look for a generic one based on the class + subclass
        //+ program interface.
        const uint32_t specificId = addr.ReadReg(0);
        const uint32_t generalId = addr.ReadReg(2);

        using namespace Drivers;
        const uint8_t idMachName[] = 
        {
            PciIdMatching,
            (uint8_t)(specificId >> 0),
            (uint8_t)(specificId >> 8),
            (uint8_t)(specificId >> 16),
            (uint8_t)(specificId >> 24),
        };
        const uint8_t classMachName[] = 
        {
            PciClassMatching,
            (uint8_t)(generalId >> 8),
            (uint8_t)(generalId >> 16),
            (uint8_t)(generalId >> 24),
        };

        const uint16_t vendorId = specificId & 0xFFFF;
        const uint16_t deviceId = specificId >> 16;

        auto maybeIdDriver = DriverManager::Global()->FindDriver(DriverSubsystem::PCI, { .length = 5, .name = idMachName });
        auto maybeClassDriver = DriverManager::Global()->FindDriver(DriverSubsystem::PCI, { .length = 4, .name = classMachName });

        if (maybeIdDriver)
        {
            PciBridge::Global()->pendingDriverLoads->EmplaceBack(addr, *maybeIdDriver);
            Logf("PCI function (%0hx:%0hx) will load id-matched driver: %s", LogSeverity::Info, vendorId, deviceId, maybeIdDriver.Value()->name);
        }
        else if (maybeClassDriver)
        {
            PciBridge::Global()->pendingDriverLoads->EmplaceBack(addr, *maybeClassDriver);
            Logf("PCI function (%0hx:%0hx) will load class-matched driver: %s", LogSeverity::Info, vendorId, deviceId, maybeClassDriver.Value()->name);
        }
        else
        {
            Logf("PCI function (%0hx:%0hx) has no driver available.", LogSeverity::Verbose, vendorId, deviceId);
        }
    }

    void PciSegmentGroup::Init()
    {
        const bool ecamAvail = PciBridge::Global()->EcamAvailable();
        
        /*
            Recursive scan: we scan all devices on known busses.
            We assume bus 0 exists (otherwise wtf are we doing here), and then
            add the secondary bus from any pci-pci bridges we encounter.
        */

        sl::Vector<uint8_t> bussesRemaining;
        bussesRemaining.PushBack(0);

        while (!bussesRemaining.Empty())
        {
            const uint8_t bus = bussesRemaining.PopBack();

            for (size_t dev = 0; dev < 32; dev++)
            {
                PciAddress addr = ecamAvail ? PciAddress::CreateEcam(baseAddress.raw, bus, dev, 0, 0) : PciAddress::CreateLegacy(bus, dev, 0, 0);
                const uint32_t reg0 = addr.ReadReg(0);

                if ((reg0 & 0xFFFF) == PciVendorNonExistant)
                    continue; //no d(ev)ice

                //check if we're multi-function or not
                const size_t maxFunctions = ((addr.ReadReg(3) >> 16) & 0x80) != 0 ? 8 : 1;
                for (size_t func = 0; func < maxFunctions; func++)
                {
                    PciAddress funcAddr = ecamAvail ? PciAddress::CreateEcam(baseAddress.raw, bus, dev, func, 0) : PciAddress::CreateLegacy(bus, dev, func, 0);
                    
                    if ((funcAddr.ReadReg(0) & 0xFFFF) == PciVendorNonExistant)
                        continue;

                    PciFunction& function = functions.EmplaceBack();
                    function.id = func;
                    function.deviceId = dev;
                    function.busId = bus;
                    function.address = funcAddr;

                    //TODO: would be nice to parse pci-ids or something here, for prettier output
                    const uint32_t functionId = funcAddr.ReadReg(0);
                    const uint32_t funcClass = funcAddr.ReadReg(2);
                    Logf("Discovered PCI function %x::%0hhx:%0hhx.%x: id=%0hx:%0hx, class=0x%x, subclass=0x%x, progIf=0x%x", LogSeverity::Verbose, 
                        id, bus, dev, func, functionId & 0xFFFF, functionId >> 16, funcClass >> 24, (funcClass >> 16) & 0xFF, (funcClass >> 8) & 0xFF);

                    if ((funcClass >> 24) == 0x6 && (uint8_t)(funcClass >> 16) == 0x4)
                    {
                        //function is PCI-to-PCI bridge, scan it's secondary bus.
                        const uint32_t secondaryBus = funcAddr.ReadReg(6);
                        bussesRemaining.PushBack((secondaryBus >> 16) & 0xFF);
                    }

                    TryFindDrivers(funcAddr);
                }
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
        const ACPI::MCFG* mcfg = reinterpret_cast<ACPI::MCFG*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MCFG));
        auto forceLegacyCfgSlot = Configuration::Global()->Get("pci_force_legacy_access");
        if (forceLegacyCfgSlot && forceLegacyCfgSlot->integer == true)
            mcfg = nullptr;

        if (mcfg != nullptr)
        {
            ecamAvailable = true;

            //discover all pci segment groups
            size_t segmentCount = (mcfg->length - sizeof(ACPI::MCFG)) / sizeof(ACPI::EcamEntry);
            for (size_t i = 0; i < segmentCount; i++)
            {
                const ACPI::EcamEntry* entry = &mcfg->entries[i];

                PciSegmentGroup& latestSeg = segments->EmplaceBack();
                latestSeg.id = entry->pciSegmentGroup;
                latestSeg.baseAddress = entry->baseAddress;
                Logf("PCIe segment group added: id=0x%lx, baseAddress=0x%lx", LogSeverity::Verbose, entry->pciSegmentGroup, entry->baseAddress);
            }

            Logf("MCFG parsed, %lu PCIe segment groups located", LogSeverity::Info, segments->Size());
        }
        else
        {
            ecamAvailable = false;
            
            //create segment group 0 to hold the root pci bus
            PciSegmentGroup& latestSeg = segments->EmplaceBack();
            latestSeg.id = 0;
            latestSeg.baseAddress = nullptr;
            Log("PCIe ecam not available, using legacy pci config mechanism.", LogSeverity::Info);
        }

        //create a map of the current pci devices
        for (auto it = segments->Begin(); it != segments->End(); ++it)
            it->Init();

        Logf("PCI bridge initialized, ecamSupport=%b", LogSeverity::Info, ecamAvailable);

        //load any drivers we detected
        for (auto it = pendingDriverLoads->Begin(); it != pendingDriverLoads->End(); ++it)
        {
            Drivers::DriverInitTagPci initTag(it->address);
            Drivers::DriverManager::Global()->StartDriver(it->manifest, &initTag);
        }

        Logf("PCI bridge has delay loaded %u drivers.", LogSeverity::Verbose, pendingDriverLoads->Size());
        pendingDriverLoads->Clear();
        delete pendingDriverLoads;
        pendingDriverLoads = nullptr;
    }

    sl::Vector<PciAddress> PciBridge::FindFunctions(uint16_t vendorId, uint16_t deviceId, bool onlyOne)
    {
        sl::Vector<PciAddress> found;
        
        for (size_t segment = 0; segment < segments->Size(); segment++)
        {
            const PciSegmentGroup& seg = segments->At(segment);
            for (size_t i = 0 ; i < seg.functions.Size(); i++)
            {
                const uint32_t functionId = seg.functions[i].address.ReadReg(0);
                if ((functionId & 0xFFFF) != vendorId || (functionId >> 16) != deviceId)
                    continue;

                found.PushBack(seg.functions[i].address);
                if (onlyOne)
                    return found;
            }
        }

        return found;
    }
}
