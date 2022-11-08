#include <devices/PciBridge.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>

namespace Npk::Devices
{
    void PciBridge::ScanSegment(uintptr_t segmentBase, uint16_t segId, bool ecamAvail)
    {
        //we assume bus 0 exists, and only scan other busses if we know they exist.
        sl::Vector<uint8_t> busses;
        busses.PushBack(0);

        while (!busses.Empty())
        {
            const uint8_t bus = busses.PopBack();

            for (size_t dev = 0; dev < 32; dev++)
            {
                const PciAddress addr = ecamAvail ? PciAddress::CreateMmio(segmentBase, bus, dev, 0) : PciAddress::CreateLegacy(bus, dev, 0);
                const uint32_t idReg = addr.ReadReg(PciReg::Id);

                if ((idReg & 0xFFFF) == 0xFFFF)
                    continue; //invalid device
                
                const size_t functionCount = ((addr.ReadReg(PciReg::LatencyCache) >> 16) & 0x80) != 0 ? 8 : 1;
                for (size_t func = 0; func < functionCount; func++)
                {
                    const PciAddress funcAddr = ecamAvail ? PciAddress::CreateMmio(segmentBase, bus, dev, func) : PciAddress::CreateLegacy(bus, dev, func);

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
        
        auto maybeMcfg = Config::FindAcpiTable(Config::SigMcfg);
        Config::Mcfg* mcfg = maybeMcfg ? static_cast<Config::Mcfg*>(*maybeMcfg) : nullptr;

        if (mcfg == nullptr)
        {
            //ECAM not available, use legacy mechanism
            Log("PCIe ECAM not available, using legacy mechanism.", LogLevel::Verbose);
            ScanSegment(0, 0, false);
        }
        else
        {
            //discover and scan all segment groups
            const size_t segmentCount = ((mcfg->length - sizeof(Config::Mcfg)) / sizeof(Config::McfgSegment));
            for (size_t i = 0; i < segmentCount; i++)
            {
                const Config::McfgSegment* seg = &mcfg->segments[i];
                ScanSegment(seg->base, seg->id, true);
                Log("PCIe segment added: base=0x%lx, id=%u, firstBus=%u, lastBus=%u", LogLevel::Verbose, 
                    seg->base, seg->id, seg->firstBus, seg->lastBus);
            }
        }

        Log("PCI bridge finished scanning, found %lu endpoints.", LogLevel::Info, addresses.Size());
        //TODO: tell driver manager new devices were found.
    }
}
