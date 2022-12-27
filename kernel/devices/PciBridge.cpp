#include <devices/PciBridge.h>
#include <config/AcpiTables.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>

namespace Npk::Devices
{
    void PciBridge::ScanSegment(uintptr_t segmentBase, uint16_t segId, bool ecamAvail)
    {
        //convinience wrapper
        auto MakeAddr = [=](uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func)
        {
            return ecamAvail ? PciAddress::CreateMmio(base, bus, dev, func) : PciAddress::CreateLegacy(bus, dev, func);
        };
        
        //we assume bus 0 exists, and only scan other busses if we know they exist.
        sl::Vector<uint8_t> busses;
        busses.PushBack(0);

        while (!busses.Empty())
        {
            const uint8_t bus = busses.PopBack();

            for (size_t dev = 0; dev < 32; dev++)
            {
                const PciAddress addr = MakeAddr(segmentBase, bus, dev, 0);
                if ((addr.ReadReg(PciReg::Id) & 0xFFFF) == 0xFFFF)
                    continue; //invalid device
                
                const size_t functionCount = ((addr.ReadReg(PciReg::LatencyCache) >> 16) & 0x80) != 0 ? 8 : 1;
                for (size_t func = 0; func < functionCount; func++)
                {
                    const PciAddress funcAddr = MakeAddr(segmentBase, bus, dev, func);
                    if ((funcAddr.ReadReg(PciReg::Id) & 0xFFFF) == 0xFFFF)
                        continue;
                    
                    addresses.PushBack(funcAddr);
                    const uint32_t funcId = funcAddr.ReadReg(PciReg::Id);
                    const uint32_t funcClass = funcAddr.ReadReg(PciReg::Class);
                    Log("Found PCI function %x:%02hx:%02hx.%01hx: id=%04x:%04x, class=0x%x:0x%x, iface=0x%x", LogLevel::Verbose, 
                        segId, bus, (uint8_t)dev, (uint8_t)func, funcId & 0xFFFF, funcId >> 16, 
                        funcClass >> 24, (funcClass >> 16) & 0xFF, (funcClass >> 8) & 0xFF);
                }
            }
        }
    }

    PciBridge globalPciBridge;
    PciBridge& PciBridge::Global()
    { return globalPciBridge; }

    void PciBridge::Init()
    {
        sl::ScopedLock scopeLock(lock);
        
        if (auto maybeMcfg = Config::FindAcpiTable(Config::SigMcfg); maybeMcfg.HasValue())
        {
            Config::Mcfg* mcfg = static_cast<Config::Mcfg*>(*maybeMcfg);

            //discover and scan all segment groups
            const size_t segmentCount = ((mcfg->length - sizeof(Config::Mcfg)) / sizeof(Config::McfgSegment));
            for (size_t i = 0; i < segmentCount; i++)
            {
                const Config::McfgSegment* seg = &mcfg->segments[i];
                Log("PCIe segment added: base=0x%lx, id=%u, firstBus=%u, lastBus=%u", LogLevel::Verbose, 
                    seg->base, seg->id, seg->firstBus, seg->lastBus);

                ScanSegment(seg->base, seg->id, true);
            }
        }
        else if (auto maybeDtNode = Config::DeviceTree::Global().GetCompatibleNode("pci-host-ecam-generic"); maybeDtNode.HasValue())
        {
            auto regProp = maybeDtNode->GetProp("reg");
            uintptr_t base;
            size_t length;
            ASSERT(regProp->ReadRegs(*maybeDtNode, &base, &length) == 1, "Unexpected register count for PCI bridge");
            Log("PCIe segment added: base=0x%lx, length=0x%lx", LogLevel::Verbose, base, length);

            ScanSegment(base, 0, true);
        }
        else
#ifdef __x86_64__
        {
            //fallback to legacy mechanism
            Log("PCIe ECAM not available, using x86 legacy mechanism.", LogLevel::Verbose);
            ScanSegment(0, 0, false);
        }
#else
        { 
            Log("No known mechanism for PCI discovery available.", LogLevel::Error); 
            return;
        }
#endif

        Log("PCI bridge finished scanning, found %lu endpoints.", LogLevel::Info, addresses.Size());

        using namespace Drivers;

        uint8_t nameBuffer[10] { "pcic\0\0\0\0\0" };
        ManifestName manifestName { 0, nameBuffer };

        for (size_t i = 0; i < addresses.Size(); i++)
        {
            //TODO: allocating and then passing this off to another thread, would be nice to have a cleaner design here.
            PciInitTag* initTag = new PciInitTag(addresses[i], nullptr);

            //look for a pci device (pcid) driver.
            const uint32_t funcId = addresses[i].ReadReg(PciReg::Id);
            nameBuffer[3] = 'd';
            nameBuffer[4] = (funcId >> 8)  & 0xFF;
            nameBuffer[5] = (funcId >> 0)  & 0xFF;
            nameBuffer[6] = (funcId >> 24) & 0xFF;
            nameBuffer[7] = (funcId >> 16) & 0xFF;
            manifestName.length = 8;
            if (DriverManager::Global().TryLoadDriver(manifestName, initTag))
                continue;

            //search for a matching pci class (pcic).
            const uint32_t funcClass = addresses[i].ReadReg(PciReg::Class);
            nameBuffer[3] = 'c';
            nameBuffer[4] = (funcClass >> 24) & 0xFF;
            nameBuffer[5] = (funcClass >> 16) & 0xFF;
            nameBuffer[6] = (funcClass >> 8)  & 0xFF;
            manifestName.length = 7;
            if (DriverManager::Global().TryLoadDriver(manifestName, initTag))
                continue;

            Log("PCI function %04x:%04x has no driver.", LogLevel::Verbose, funcId & 0xFFFF, funcId >> 16);
            delete initTag;
        }
    }
}
