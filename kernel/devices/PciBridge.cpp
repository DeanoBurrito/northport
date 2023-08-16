#include <devices/PciBridge.h>
#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>

namespace Npk::Devices
{
    constexpr const char* SpaceTypeStrs[] =
    {
        "Config", "IO", "32-bit memory", "64-bit memory", "DMA"
    };
    
    void PciBridge::ScanSegment(uintptr_t segmentBase, uint8_t startBus, uint16_t segId, bool ecamAvail)
    {
        //convinience wrapper
        auto MakeAddr = [=](uintptr_t base, uint8_t bus, uint8_t dev, uint8_t func)
        {
            return ecamAvail 
                ? PciAddress::CreateEcam(base, bus - startBus, dev, func) 
                : PciAddress::CreateLegacy(bus, dev, func);
        };
        
        //we assume bus 0 exists, and only scan other busses if we know they exist.
        sl::Vector<uint8_t> busses;
        busses.PushBack(startBus);

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
                    Log("Found PCI function %x:%02hx:%02hx.%01hx: id=%04x:%04x, class=%02x:%02x.%02x", LogLevel::Verbose, 
                        segId, bus, (uint8_t)dev, (uint8_t)func, funcId & 0xFFFF, funcId >> 16, 
                        funcClass >> 24, (funcClass >> 16) & 0xFF, (funcClass >> 8) & 0xFF);
                }
            }
        }
    }

    bool PciBridge::ParseTranslations(const Config::DtNode& node, sl::StringSpan propName, bool outbound)
    {
        //TODO: handle empty properties (identity mapped)
        Config::DtProp* rangesProp = node.FindProp(propName);
        if (rangesProp == nullptr)
            return false;

        //PCI <-> host address space translations are encoded with an extra field
        //which includes metadata about what that space is used for. Like is it 32/64
        //bit or io space. We also need this info so we can correctly assign BARs
        //ourselves if we've reached this point
        const size_t count = rangesProp->ReadRangesWithMeta({}, 1);
        Config::DtQuad ranges[count];
        ASSERT(rangesProp->ReadRangesWithMeta({ ranges, count }, 1) == count, "Bad DTB property");

        for (size_t i = 0; i < count; i++)
        {
            const Config::DtQuad& range = ranges[i];
            PciSpaceType type = PciSpaceType::Dma;
            if (outbound)
                type= (PciSpaceType)((range[3] >> 24) & 0b11);

            maps.EmplaceBack(type, range[1], range[0], range[2]);
            Log("PCI/host address space window: host=0x%lx, pci=0x%lx, length=0x%lx, type=%s",
                LogLevel::Verbose, range[1], range[0], range[2], SpaceTypeStrs[(size_t)type]);
        }

        return true;
    }

    uintptr_t PciBridge::AllocForBar(PciSpaceType type, size_t length)
    {
        ASSERT(!maps.Empty(), "Tried to alloc PCI BAR with no ranges.");

        for (size_t i = 0; i < maps.Size(); i++)
        {
            if (maps[i].type != type)
                continue;
            
            PciSpaceMap& range = maps[i];
            
            if (range.allocated + length > range.length)
                continue; //not enough space for this BAR
            
            const uintptr_t address = range.pciBase + range.allocated;
            range.allocated += length;
            return address;
        }

        return 0;
    }

    void PciBridge::AllocateBars()
    {
        /* 
            X86 is pretty good at setting up PCI before handing over control to
            software, but other platforms don't do this. This function checks all
            the discovered PCI functions and tries to allocate BARs for them.
            After skipping unimplemented BARs, if we find one with a non-zero address
            we know firmware has already set up everything for us.
        */
        size_t allocCount = 0;
        for (size_t i = 0; i < addresses.Size(); i++)
        {
            const PciAddress addr = addresses[i];
            
            //try allocate each BAR if it is implemented,
            //umimplemented BARs have a type of 0 and size of 0
            for (size_t bi = 0; bi < 6; bi++)
            {
                const size_t offset = ((size_t)PciReg::Bar0 + bi) * 4;
                const uint32_t value = addr.ReadAt(offset);
                addr.WriteAt(offset, 0xFFFF'FFFF);
                const uint32_t mask = addr.ReadAt(offset) & ~(uint32_t)0xF;

                if (value == 0 && mask == 0)
                    continue; //BAR is not implemented
                
                const uint32_t controlMask = (value & 0b1) ? 0b11 : 0b1111;
                if ((value & ~controlMask) != 0)
                {
                    addr.WriteAt(offset, value);
                    Log("Skipping PCI BAR allocation, firmware has done it.", LogLevel::Info);
                    return;
                }

                uint64_t upperMask = 0xFFFF'FFFF;
                const bool is64Bit = (value & 0b100) && (controlMask > 0b11); //IO space is never 64bit
                if (is64Bit)
                {
                    addr.WriteAt(offset + 4, 0xFFFF'FFFF);
                    upperMask = addr.ReadAt(offset + 4);
                }

                //Yes I have forgotten the operator precedences for all this.
                const size_t barLength = (~(upperMask << 32 | mask)) + 1;
                const PciSpaceType spaceType = controlMask == 0b11 ? PciSpaceType::BarIo : 
                    (is64Bit ? PciSpaceType::Bar64 : PciSpaceType::Bar32);
                const uintptr_t barAddr = AllocForBar(spaceType, barLength);
                ASSERT(barAddr != 0, "Failed to allocate for BAR.");

                addr.WriteAt(offset, barAddr | (value & controlMask));
                if (is64Bit)
                {
                    addr.WriteAt(offset + 4, barAddr >> 32);
                    bi++; //we already handled the next BAR in this case, so dont try allocate it.
                }

                allocCount++;
            }
        }

        Log("Allocated %lu PCI BARs.", LogLevel::Info, allocCount);
    }

    void PciBridge::DetectDrivers()
    {
        using namespace Drivers;
        uint8_t nameBuffer[10] { "pcic\0\0\0\0\0" };
        ManifestName manifestName;

        size_t loadedCount = addresses.Size();
        for (size_t i = 0; i < addresses.Size(); i++)
        {
            PciInitTag* initTag = new PciInitTag(addresses[i], nullptr);

            //look for a pci device (pcid) driver.
            const uint32_t funcId = addresses[i].ReadReg(PciReg::Id);
            nameBuffer[3] = 'd';
            nameBuffer[4] = (funcId >> 8)  & 0xFF;
            nameBuffer[5] = (funcId >> 0)  & 0xFF;
            nameBuffer[6] = (funcId >> 24) & 0xFF;
            nameBuffer[7] = (funcId >> 16) & 0xFF;
            manifestName = ManifestName(nameBuffer, 8);
            if (DriverManager::Global().TryLoadDriver(manifestName, initTag))
                continue;

            //search for a matching pci class (pcic).
            const uint32_t funcClass = addresses[i].ReadReg(PciReg::Class);
            nameBuffer[3] = 'c';
            nameBuffer[4] = (funcClass >> 24) & 0xFF;
            nameBuffer[5] = (funcClass >> 16) & 0xFF;
            nameBuffer[6] = (funcClass >> 8)  & 0xFF;
            manifestName = ManifestName(nameBuffer, 7);
            if (DriverManager::Global().TryLoadDriver(manifestName, initTag))
                continue;

            Log("PCI function %04x:%04x has no driver.", LogLevel::Verbose, funcId & 0xFFFF, funcId >> 16);
            delete initTag;
            loadedCount--;
        }

        Log("PCI bridge started %lu drivers, %lu unknown functions.", LogLevel::Info,
            loadedCount, addresses.Size() - loadedCount);
    }

    PciBridge globalPciBridge;
    PciBridge& PciBridge::Global()
    { return globalPciBridge; }

    void PciBridge::Init()
    {
        using namespace Config;
        sl::ScopedLock scopeLock(lock);
        
        if (auto maybeMcfg = FindAcpiTable(SigMcfg); maybeMcfg.HasValue())
        {
            const Mcfg* mcfg = static_cast<const Mcfg*>(*maybeMcfg);

            //discover and scan all segment groups
            const size_t segmentCount = ((mcfg->length - sizeof(Mcfg)) / sizeof(McfgSegment));
            for (size_t i = 0; i < segmentCount; i++)
            {
                const McfgSegment* seg = &mcfg->segments[i];
                Log("PCIe segment added: base=0x%lx, id=%u, firstBus=%u, lastBus=%u", LogLevel::Verbose, 
                    seg->base, seg->id, seg->firstBus, seg->lastBus);

                ScanSegment(seg->base, seg->firstBus, seg->id, true);
            }
        }
        else if (DtNode* node = DeviceTree::Global().FindCompatible("pci-host-ecam-generic"); node != nullptr)
        {
            auto regProp = node->FindProp("reg");
            DtPair reg;
            ASSERT(regProp->ReadRegs({ &reg, 1 }) == 1, "Unexpected register count for PCI bridge");

            ASSERT(ParseTranslations(*node, "ranges", true), "Failed to pass PCI DTB ranges");
            if (!ParseTranslations(*node, "dma-ranges", false))
            {
                Log("Failed to parse dma-ranges node for PCI bridge, behaviour maybe be broken.",
                    LogLevel::Warning);
            }

            auto busProp = node->FindProp("bus-range");
            ASSERT(busProp, "Not starting bus for PCI bridge");
            const uint8_t firstBus = busProp->ReadValue(1);

            Log("PCIe segment added: base=0x%lx, length=0x%lx, firstBus=%u", 
                LogLevel::Verbose, reg[0], reg[1], firstBus);
            ScanSegment(reg[0], firstBus, 0, true);
        }
        else
#ifdef __x86_64__
        {
            //fallback to legacy mechanism
            Log("PCIe ECAM not available, using x86 legacy mechanism.", LogLevel::Verbose);
            ScanSegment(0, 0, 0, false);
        }
#else
        { 
            Log("No known mechanism for PCI discovery available.", LogLevel::Error); 
            return;
        }
#endif

        Log("PCI bridge finished scanning, found %lu functions.", LogLevel::Info, addresses.Size());
        AllocateBars();
        DetectDrivers();
    }

    sl::Opt<uintptr_t> PciBridge::PciToHost(uintptr_t addr, PciSpaceType type)
    {
        if (maps.Empty())
            return addr; //no translations, assume identity map (otherwise we wouldn't be operating at all).
        
        for (size_t i = 0; i < maps.Size(); i++)
        {
            if (maps[i].type != type)
                continue;
            if (addr < maps[i].pciBase || addr >= maps[i].pciBase + maps[i].length)
                continue;

            return addr - maps[i].pciBase + maps[i].physBase;
        }

        Log("PCI -> phys translation failed: pci=0x%lx, type=%s", LogLevel::Error, 
            addr, SpaceTypeStrs[(size_t)type]);
        return {};
    }

    sl::Opt<uintptr_t> PciBridge::HostToPci(uintptr_t addr)
    {
        if (maps.Empty())
            return addr;
        
        for (size_t i = 0; i < maps.Size(); i++)
        {
            if (maps[i].type != PciSpaceType::Dma)
                continue;
            if (addr < maps[i].physBase || addr >= maps[i].physBase + maps[i].length)
                continue;
            
            return addr - maps[i].physBase + maps[i].pciBase;
        }

        Log("Phys -> PCI translation failed: host=0x%lx", LogLevel::Error, addr);
        return {};
    }
}
