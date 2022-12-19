#include <drivers/builtin/BochsVga.h>
#include <drivers/InitTags.h>
#include <devices/DeviceManager.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <tasking/Thread.h>

#define BGA_DISPI_DISABLE 0x0
#define BGA_DISPI_ENABLE 0x1
#define BGA_DISPI_LFB_ENABLED 0x40
#define BGA_DISPI_NO_CLEAR_MEM 0x80

namespace Npk::Drivers
{
    void BochsVgaMain(void* arg)
    {
        InitTag* tags = static_cast<InitTag*>(arg);
        while (tags != nullptr && tags->type != InitTagType::Pci)
            tags = tags->next;
        
        if (tags == nullptr)
        {
            Log("Bochs VGA init failed, no pci address.", LogLevel::Error);
            Tasking::Thread::Current().Exit(1); //TODO: drivers should have their own exit function.
        }

        //the bochs gpu only supports a single framebuffer, so there's no detection/count logic.
        BochsFramebuffer* framebuffer = new BochsFramebuffer(static_cast<PciInitTag*>(tags)->address);

        using namespace Devices;
        auto maybeDeviceId = Devices::DeviceManager::Global().AttachDevice(framebuffer);
        if (!maybeDeviceId)
        {
            delete framebuffer;
            Log("Bochs BGA init failed, could not attach device.", LogLevel::Error);
            Tasking::Thread::Current().Exit(1);
        }

        //cleanup the init tags
        tags = static_cast<InitTag*>(arg);
        while (tags != nullptr)
        {
            InitTag* temp = tags;
            tags = tags->next;
            delete temp;
        }

        Log("Bochs VGA init done. Framebuffer device id=%lu", LogLevel::Debug, *maybeDeviceId);

        while (true)
        {}
    }

    void BochsFramebuffer::WriteVgaReg(uint16_t reg, uint16_t data) const
    {
        mmioRegs.Offset(reg - 0x3C0 + 0x400).Write(data);
    }

    uint16_t BochsFramebuffer::ReadVgaReg(uint16_t reg) const
    {
        return mmioRegs.Offset(reg - 0x3C0 + 0x400).Read<uint16_t>();
    }

    void BochsFramebuffer::WriteDispiReg(DispiReg reg, uint16_t data) const
    {
        mmioRegs.Offset(0x500).Offset((uint16_t)reg << 1).Write(data);
    }

    uint16_t BochsFramebuffer::ReadDispiReg(DispiReg reg) const
    {
        return mmioRegs.Offset(0x500).Offset((uint16_t)reg << 1).Read<uint16_t>();
    }
    
    bool BochsFramebuffer::Init()
    {
        sl::ScopedLock scopeLock(lock);

        if (status != Devices::DeviceStatus::Offline)
            return false;
        status = Devices::DeviceStatus::Starting;
        
        const uint32_t subclass = addr.ReadReg(Devices::PciReg::Class) >> 16;
        const Devices::PciBar bar2 = addr.ReadBar(2);
        ASSERT(subclass == 0x80 || bar2.size > 0, "Only legacy-free is supported, update your emulator.");

        auto maybeRegs = VMM::Kernel().Alloc(bar2.size, bar2.address, VmFlags::Mmio | VmFlags::Write);
        ASSERT(maybeRegs, "Failed to alloc VM space for bochs VGA regs.");
        mmioRegs = maybeRegs->base;

        WriteDispiReg(DispiReg::Enable, BGA_DISPI_DISABLE);
        width = ReadDispiReg(DispiReg::XRes);
        height = ReadDispiReg(DispiReg::YRes);
        bpp = ReadDispiReg(DispiReg::Bpp);
        WriteDispiReg(DispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED | BGA_DISPI_NO_CLEAR_MEM);

        const Devices::PciBar bar0 = addr.ReadBar(0);
        auto maybeFramebuffer = VMM::Kernel().Alloc(bar0.size, bar0.address, VmFlags::Mmio | VmFlags::Write);
        ASSERT(maybeFramebuffer, "Failed to alloc VM spce for bochs VGA framebuffer.");
        fbBase = maybeFramebuffer->base;

        status = Devices::DeviceStatus::Online;
        return true;
    }

    bool BochsFramebuffer::Deinit()
    {
        sl::ScopedLock scopeLock(lock);

        VMM::Kernel().Free(fbBase.raw);
        VMM::Kernel().Free(mmioRegs.raw);
        fbBase = mmioRegs = nullptr;

        ASSERT_UNREACHABLE();
    }

    bool BochsFramebuffer::CanModeset()
    { return true; }

    Devices::FramebufferMode BochsFramebuffer::CurrentMode()
    { 
        return { width, height, bpp, {}};
    }

    bool BochsFramebuffer::SetMode(const Devices::FramebufferMode& newMode)
    { 
        ASSERT(newMode.bpp == 32, "Only 32bpp is supported.");
        //TODO: check pixel format matches, also add prefixed colour formats.

        sl::ScopedLock scopeLock(lock);
        
        width = sl::Clamp<uint16_t>(newMode.width, 1, 1024);
        height = sl::Clamp<uint16_t>(newMode.height, 1, 768);
        bpp = sl::Clamp<uint16_t>(newMode.bpp, 4, 32);
        
        WriteDispiReg(DispiReg::Enable, BGA_DISPI_DISABLE);
        WriteDispiReg(DispiReg::XRes, width);
        width = ReadDispiReg(DispiReg::XRes);

        WriteDispiReg(DispiReg::YRes, height);
        height = ReadDispiReg(DispiReg::YRes);

        WriteDispiReg(DispiReg::Bpp, bpp);
        bpp = ReadDispiReg(DispiReg::Bpp);
        //dont set NO_CLEAR_MEM since a resize trashes the framebuffer contents.
        WriteDispiReg(DispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED);

        return true;
    }

    sl::NativePtr BochsFramebuffer::LinearAddress()
    { return fbBase; }
}