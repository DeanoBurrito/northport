#include <devices/PciBridge.h>
#include <config/AcpiTables.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <drivers/DriverManager.h>
#include <memory/VmObject.h>

namespace Npk::Devices
{
    constexpr const char* PciNameFormatStr = "pci:%02hx:%02hx:%01hx (v=%04x:%04x c=%02x:%02x:%02x)";

    static uint32_t ReadReg(PciAddr addr, uint8_t index)
    {
#ifdef __x86_64__
        if (addr.segmentBase == 0)
        {
            const uint32_t pciAddr = 0x8000'0000 | (addr.bus << 16) | (addr.device << 11) 
                | (addr.function << 8) | (index * 4);
            Out32(PortPciAddr, pciAddr);
            return In32(PortPciData);
        }
#endif
        //rip performance, mapping virtual memory for every pci-space access - lets cache this info TODO:
        const uintptr_t physAddr = ((addr.bus << 20) | (addr.device << 15) 
            | (addr.function << 12) | (index * 4));
        VmObject access(0x1000, addr.segmentBase + physAddr, VmFlag::Mmio);
        ASSERT_(access.Valid());
        return access->Read<uint32_t>();
    }

    static void RegisterDevice(PciAddr addr)
    {
        using namespace Drivers;
        
        const uint32_t vendor = ReadReg(addr, 0);
        const uint32_t type = ReadReg(addr, 2);

        const size_t nameLen = npf_snprintf(nullptr, 0, PciNameFormatStr, 
            addr.bus, addr.device, addr.function, 0, 0, 0, 0, 0) + 1;
        char* nameBuff = new char[nameLen];
        npf_snprintf(nameBuff, nameLen, PciNameFormatStr, addr.bus, addr.device,
            addr.function, vendor & 0xFFFF, vendor >> 16, type >> 24, (type >> 16) & 0xFF,
            (type >> 8) & 0xFF);
        nameBuff[nameLen - 1] = 0;

        sl::Handle<DeviceDescriptor> desc = new DeviceDescriptor();
        desc->friendlyName = sl::String(nameBuff, true);
        desc->initData = nullptr; //TODO: populate these properly

        uint8_t* classBuffer = new uint8_t[3]; //TODO: leaking memory?
        classBuffer[0] = type >> 24;
        classBuffer[1] = type >> 16;
        classBuffer[2] = type >> 8;
        DeviceLoadName className { .type = LoadType::PciClass, .str = { classBuffer, 3 }};

        uint8_t* vendorBuffer = new uint8_t[4];
        vendorBuffer[0] = vendor;
        vendorBuffer[1] = vendor >> 8;
        vendorBuffer[2] = vendor >> 16;
        vendorBuffer[3] = vendor >> 24;
        DeviceLoadName vendorName { .type = LoadType::PciId, .str = {} };

        desc->loadNames.EnsureCapacity(2);
        desc->loadNames.PushBack(className);
        desc->loadNames.PushBack(vendorName);

        DriverManager::Global().AddDevice(desc);
    }

    void PciBridge::ScanSegment(uintptr_t segmentBase, uint8_t startBus, uint16_t segId, bool ecamAvail)
    {
        constexpr uint8_t PciRegId = 0;
        constexpr uint8_t PciRegClass = 2;
        constexpr uint8_t PciRegLatency = 3;
        
        //we assume bus 0 exists, and only scan other busses if we know they exist.
        sl::Vector<uint8_t> busses;
        busses.PushBack(startBus);

        while (!busses.Empty())
        {
            const uint8_t bus = busses.PopBack();

            for (uint8_t dev = 0; dev < 32; dev++)
            {
                PciAddr addr { .segmentBase = segmentBase, .bus = bus, .device = dev, .function = 0 };
                if ((ReadReg(addr, PciRegId) & 0xFFFF) == 0xFFFF)
                    continue;
                
                const size_t funcCount = ((ReadReg(addr, PciRegLatency) >> 16) & 0x80) != 0 ? 8 : 1;
                for (uint8_t func = 0; func < funcCount; func++)
                {
                    addr.function = func;
                    if ((ReadReg(addr, PciRegId) & 0xFFFF) == 0xFFFF)
                        continue;

                    RegisterDevice(addr);
                }
            }
        }
    }

    bool PciBridge::TryInitFromAcpi()
    {
        using namespace Config;
        auto maybeMcfg = Config::FindAcpiTable(SigMcfg);
        if (!maybeMcfg.HasValue())
            return false;

        const Mcfg* mcfg = static_cast<const Mcfg*>(*maybeMcfg);
        const size_t segmentCount = ((mcfg->length - sizeof(Mcfg)) / sizeof(McfgSegment));

        for (size_t i = 0; i < segmentCount; i++)
        {
            const McfgSegment* seg = &mcfg->segments[i];
            Log("PCIe segment added: base=0x%lx, id=%u, firstBus=%u, lastBus=%u", LogLevel::Verbose, 
                seg->base, seg->id, seg->firstBus, seg->lastBus);

            ScanSegment(seg->base, seg->firstBus, seg->id, true);
        }

        return true;
    }

    bool PciBridge::TryInitFromDtb()
    {
        using namespace Config;

        DtNode* node = DeviceTree::Global().FindCompatible("pci-host-ecam-generic");
        if (node == nullptr)
            return false;
        auto regProp = node->FindProp("reg");
        DtPair reg;
        ASSERT(regProp->ReadRegs({ &reg, 1 }) == 1, "Unexpected register count for PCI bridge");

        auto busProp = node->FindProp("bus-range");
        ASSERT(busProp, "Not starting bus for PCI bridge");
        const uint8_t firstBus = busProp->ReadValue(1);

        Log("PCIe segment added: base=0x%lx, length=0x%lx, firstBus=%u", 
            LogLevel::Verbose, reg[0], reg[1], firstBus);
        ScanSegment(reg[0], firstBus, 0, true);

        return true;
    }

    PciBridge globalPciBridge;
    PciBridge& PciBridge::Global()
    { return globalPciBridge; }

    void PciBridge::Init()
    {
        using namespace Config;
        sl::ScopedLock scopeLock(lock);
        
        bool initialized = TryInitFromAcpi();
        if (!initialized)
            initialized = TryInitFromDtb();
        if (!initialized)
#ifdef __x86_64__
        {
            //fallback to legacy mechanism
            Log("PCIe ECAM not available, using x86 legacy mechanism.", LogLevel::Verbose);
            ScanSegment(0, 0, 0, false);
        }
#else
        {
            Log("No known mechanism for PCI discovery.", LogLevel::Error);
            return;
        }
#endif
    }
}
