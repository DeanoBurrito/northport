#include <devices/pci/PciEndpoints.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>
#include <Log.h>

namespace Kernel::Devices::Pci
{
    constexpr uint16_t VendorIdNonexistent = 0xffff;
    
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

            if ((reg0 & 0xFFFF) == VendorIdNonexistent)
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
                PciBridge::Global()->pendingDriverLoads->EmplaceBack(functions.Back().addr, driver.Value());
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
}
