#include <devices/pci/BochsGraphicsAdaptor.h>
#include <devices/interfaces/GenericGraphicsAdaptor.h>
#include <devices/DeviceManager.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <Maths.h>
#include <Log.h>
#include <Locks.h>

#define BGA_DISPI_DISABLE 0x0
#define BGA_DISPI_ENABLE 0x1
#define BGA_DISPI_LFB_ENABLED 0x40
#define BGA_DISPI_NO_CLEAR_MEM 0x80

namespace Kernel::Devices::Pci
{
    Drivers::GenericDriver* CreateNewBgaDriver()
    { return new BochsGraphicsDriver(); }

    using namespace Interfaces;

    void BochsGraphicsDriver::Init(Drivers::DriverInitInfo* info)
    {
        (void)info;

        adaptor = new BochsGraphicsAdaptor();
        DeviceManager::Global()->RegisterDevice(adaptor);
        DeviceManager::Global()->SetPrimaryDevice(DeviceType::GraphicsAdaptor, adaptor->GetId());
    }

    void BochsGraphicsDriver::Deinit()
    {
        delete DeviceManager::Global()->UnregisterDevice(adaptor->GetId());
        adaptor = nullptr;
    }

    void BochsGraphicsDriver::HandleEvent(Drivers::DriverEventType type, void* arg)
    { 
        (void)type; (void)arg;
    }

    enum BgaDispiReg : uint16_t
    {
        Id = 0,
        XRes = 1,
        YRes = 2,
        Bpp = 3,
        Enable = 4,
        Bank = 5,
        VirtWidth = 6,
        VirtHeight = 7,
        XOffset = 8,
        YOffset = 9,
    };

    void BochsFramebuffer::WriteVgaReg(uint16_t reg, uint16_t data) const
    {
        if (mmioBase.ptr != nullptr)
            sl::MemWrite<uint16_t>(mmioBase.raw + reg - 0x3c0 + 0x400, data);
        else
            CPU::PortWrite16(reg, data);
    }

    uint16_t BochsFramebuffer::ReadVgaReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + reg - 0x3c0 + 0x400);
        else
            return CPU::PortRead16(reg);
    }

    void BochsFramebuffer::WriteDispiReg(uint16_t reg, uint16_t data) const
    {
        if (mmioBase.ptr != nullptr)
            sl::MemWrite<uint16_t>(mmioBase.raw + 0x500 + (reg << 1), data);
        else
        {
            //magic numbers here are BGA DISPI index and data ports, respectively.
            CPU::PortWrite16(0x01CE, reg);
            CPU::PortWrite16(0x01CF, data);
        }
    }

    uint16_t BochsFramebuffer::ReadDispiReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + 0x500 + (reg << 1));
        else
        {
            CPU::PortWrite16(0x01CE, reg);
            return CPU::PortRead16(0x01CF);
        }
    }

    void BochsFramebuffer::Init()
    {
        sl::ScopedSpinlock scopeLock(&lock);
    
        if (state == DeviceState::Ready)
            return;

        type = DeviceType::GraphicsFramebuffer;

        auto maybePciDevice = PciBridge::Global()->FindDevice(0x1234, 0x1111);
        if (!maybePciDevice)
        {
            Log("Cannot initialize bochs framebuffer: matching pci device not found.", LogSeverity::Error);
            return;
        };

        auto maybePciFunction = maybePciDevice.Value()->GetFunction(0);
        if (!maybePciFunction)
        {
            Log("Cannot initialize bochs framebuffer: pci device does not have function 0.", LogSeverity::Error);
            return;
        }

        format = { 16, 8, 0, 24, 0xFF, 0xFF, 0xFF, 0 };

        const PciFunction* pciFunc = *maybePciFunction;
        bool mmioRegsAvailable = false;
        if (pciFunc->GetHeader()->ids.deviceSubclass == 0x80)
            mmioRegsAvailable = true; //qemu legacy free variant
        else if (pciFunc->GetHeader()->bars[2].size > 0)
            mmioRegsAvailable = true; //bochs standard variant, but BAR2 is populated so mmio regs are available

        linearFramebufferBase.raw = pciFunc->GetHeader()->bars[0].address;
        const size_t fbPageCount = pciFunc->GetHeader()->bars[0].size / PAGE_FRAME_SIZE; //TODO: investigate map issues here, puts framebuffer in a weird state
        // VMM::Local()->MapRange(EnsureHigherHalfAddr(linearFramebufferBase.raw), linearFramebufferBase, fbPageCount, Memory::MemoryMapFlags::AllowWrites);
        linearFramebufferBase.raw = EnsureHigherHalfAddr(linearFramebufferBase.raw);

        if (mmioRegsAvailable)
        {
            mmioBase.raw = pciFunc->GetHeader()->bars[2].address;
            const size_t mmioPageCount = pciFunc->GetHeader()->bars[2].size / PAGE_FRAME_SIZE;
            Memory::PageTableManager::Current()->MapRange(EnsureHigherHalfAddr(mmioBase.raw), mmioBase, mmioPageCount, Memory::MemoryMapFlags::AllowWrites);
            mmioBase.raw = EnsureHigherHalfAddr(mmioBase.raw);

            Log("Bochs framebuffer is legacy free variant, using mmio registers.", LogSeverity::Verbose);
        }
        else
        {
            mmioBase.ptr = nullptr;
            Log("Bochs framebuffer does not support registers via mmio, using port io instead.", LogSeverity::Warning);
        }

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_DISABLE);
        width = ReadDispiReg(BgaDispiReg::XRes);
        height = ReadDispiReg(BgaDispiReg::YRes);
        bpp = ReadDispiReg(BgaDispiReg::Bpp);
        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED | BGA_DISPI_NO_CLEAR_MEM);

        state = DeviceState::Ready;
    }

    void BochsFramebuffer::Deinit()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        state = DeviceState::Shutdown;
    }

    void BochsFramebuffer::Reset()
    {
        Deinit();
        Init();
    }

    sl::Opt<Drivers::GenericDriver*> BochsFramebuffer::GetDriverInstance()
    { return {}; }

    bool BochsFramebuffer::CanModeset() const
    { return true; }

    void BochsFramebuffer::SetMode(FramebufferModeset& modeset)
    {
        if (modeset.bitsPerPixel != 32)
            Log("Attempting to set non 32bpp mode on framebuffer, this is not currently supported! TODO:", LogSeverity::Warning);

        sl::ScopedSpinlock scopeLock(&lock);

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_DISABLE);

        WriteDispiReg(BgaDispiReg::XRes, sl::clamp<uint16_t>((uint16_t)modeset.width, 1, 1024));
        WriteDispiReg(BgaDispiReg::YRes, sl::clamp<uint16_t>((uint16_t)modeset.height, 1, 768));
        WriteDispiReg(BgaDispiReg::Bpp, sl::clamp<uint16_t>(modeset.bitsPerPixel, 4, 32));

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED | BGA_DISPI_NO_CLEAR_MEM);
    }

    FramebufferModeset BochsFramebuffer::GetCurrentMode() const
    { return { width, height, bpp, format }; }

    sl::Opt<sl::NativePtr> BochsFramebuffer::GetAddress() const
    {
        if (state != DeviceState::Ready)
            return {};

        return linearFramebufferBase;
    }

    void BochsGraphicsAdaptor::Init()
    {
        type = DeviceType::GraphicsAdaptor;
        
        if (framebuffer == nullptr)
        {
            framebuffer = new BochsFramebuffer();
            DeviceManager::Global()->RegisterDevice(framebuffer);
            DeviceManager::Global()->SetPrimaryDevice(DeviceType::GraphicsFramebuffer, framebuffer->GetId());
        }
    }

    void BochsGraphicsAdaptor::Deinit()
    {
        if (framebuffer != nullptr)
        {
            delete DeviceManager::Global()->UnregisterDevice(framebuffer->GetId());
            framebuffer = nullptr;
        }
    }

    void BochsGraphicsAdaptor::Reset()
    {
        Deinit();
        Init();
    }

    sl::Opt<Drivers::GenericDriver*> BochsGraphicsAdaptor::GetDriverInstance()
    { return {}; }

    size_t BochsGraphicsAdaptor::GetFramebuffersCount() const
    { return 1; }

    GenericFramebuffer* BochsGraphicsAdaptor::GetFramebuffer(size_t index) const
    { 
        if (index == 0)
            return framebuffer;
        else
            return nullptr;
    }
}
