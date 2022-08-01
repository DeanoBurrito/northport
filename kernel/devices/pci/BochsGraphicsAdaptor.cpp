#include <devices/pci/BochsGraphicsAdaptor.h>
#include <devices/DeviceManager.h>
#include <devices/PciBridge.h>
#include <memory/VirtualMemory.h>
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
            PortWrite16(reg, data);
    }

    uint16_t BochsFramebuffer::ReadVgaReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + reg - 0x3c0 + 0x400);
        else
            return PortRead16(reg);
    }

    void BochsFramebuffer::WriteDispiReg(uint16_t reg, uint16_t data) const
    {
        if (mmioBase.ptr != nullptr)
            sl::MemWrite<uint16_t>(mmioBase.raw + 0x500 + (reg << 1), data);
        else
        {
            //magic numbers here are BGA DISPI index and data ports, respectively.
            PortWrite16(0x01CE, reg);
            PortWrite16(0x01CF, data);
        }
    }

    uint16_t BochsFramebuffer::ReadDispiReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + 0x500 + (reg << 1));
        else
        {
            PortWrite16(0x01CE, reg);
            return PortRead16(0x01CF);
        }
    }

    void BochsFramebuffer::Init()
    {
        sl::SpinlockAcquire(&lock);
    
        if (state == DeviceState::Ready)
            return;

        type = DeviceType::GraphicsFramebuffer;

        const sl::Vector<PciAddress> foundDevices = PciBridge::Global()->FindFunctions(0x1234, 0x1111);
        if (foundDevices.Empty())
        {
            Log("Cannot initialize bochs framebuffer: matching pci device not found.", LogSeverity::Error);
            return;
        };

        PciAddress pciAddr = foundDevices[0];
        const uint8_t subclass = pciAddr.ReadReg(2) >> 16;
        if (subclass == 0x80 || pciAddr.GetBarSize(2) > 0)
        {
            mmioBase = pciAddr.ReadBar(2).address;
            Memory::PageTableManager::Current()->MapMemory(EnsureHigherHalfAddr(mmioBase.ptr), mmioBase, MFlags::AllowWrites);
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

        linearFramebufferBase.raw = pciAddr.ReadBar(0).address;
        format = { 16, 8, 0, 24, 0xFF, 0xFF, 0xFF, 0 };

        state = DeviceState::Ready;

        sl::SpinlockRelease(&lock);
        FramebufferModeset mode(-1, -1, 32, format);
        SetMode(mode);
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

        width = sl::clamp<uint16_t>((uint16_t)modeset.width, 1, 1024);
        height = sl::clamp<uint16_t>((uint16_t)modeset.height, 1, 768);
        bpp = sl::clamp<uint16_t>(modeset.bitsPerPixel, 4, 32);

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_DISABLE);

        WriteDispiReg(BgaDispiReg::XRes, width);
        WriteDispiReg(BgaDispiReg::YRes, height);
        WriteDispiReg(BgaDispiReg::Bpp, bpp);

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

        state = DeviceState::Ready;
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
