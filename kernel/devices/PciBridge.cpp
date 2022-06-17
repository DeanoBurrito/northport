#include <devices/PciBridge.h>
#include <acpi/AcpiTables.h>
#include <drivers/DriverManager.h>
#include <Configuration.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Devices
{
    PciBridge globalPciBridge;
    PciBridge* PciBridge::Global()
    { return &globalPciBridge; }

    void PciBridge::Init()
    {
        segments = new sl::Vector<PciSegmentGroup>;
        pendingDriverLoads = new sl::Vector<DelayLoadedPciDriver>();
        
        //determine which config mechanism to use, and setup segments accordingly
        ACPI::MCFG* mcfg = reinterpret_cast<ACPI::MCFG*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MCFG));
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
            Drivers::DriverInitTagPci initTag(it->address);
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

    sl::Opt<const PciFunction*> PciBridge::GetFunction(Pci::PciAddress addr)
    {
        if ((addr.addr >> 32) == 0xFFFF'FFFF)
            return GetFunction(0, (addr.addr >> 16) & 0xFF, (addr.addr >> 11) & 0b11111, (addr.addr >> 8) & 0b111);
        else
            return GetFunction(0, (addr.addr >> 20) & 0xFF, (addr.addr >> 15) & 0b11111, (addr.addr >> 12) & 0b111);
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
