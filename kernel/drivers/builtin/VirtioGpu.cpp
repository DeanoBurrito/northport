#include <drivers/builtin/VirtioGpu.h>
#include <drivers/builtin/VirtioDefs.h>
#include <drivers/InitTags.h>
#include <devices/PciCapabilities.h>
#include <devices/DeviceManager.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <tasking/Thread.h>

namespace Npk::Drivers
{
    void VirtioGpuMain(void* arg)
    {
        using namespace Devices;
        VirtioGpuDriver driver;

        //init the virtio transport layer
        if (!driver.transport.Init(static_cast<InitTag*>(arg)))
        {
            Log("Virtio transport init failed, aborting gpu driver init.", LogLevel::Error);
            Tasking::Thread::Current().Exit(1);
        }
        driver.transport.Reset();
        CleanupTags(arg);

        //init the driver instance itself
        if (!driver.Init())
        {
            Log("Virtio gpu driver init failed.", LogLevel::Error);
            Tasking::Thread::Current().Exit(2);
        }

        Log("Virtio GPU init done.", LogLevel::Debug);
        while (true)
        {}
        
        driver.Deinit();
        driver.transport.Deinit();
    }

    bool VirtioGpuDriver::SendCmd(size_t q, uintptr_t cmdAddr, size_t cmdLen, size_t respLen, sl::Opt<GpuCmdRespType> respType)
    {
        auto cmdDesc = transport.AddDescriptor(q, cmdAddr, cmdLen, false);
        VALIDATE(cmdDesc, false, "Add cmd descriptor failed");

        auto respDesc = transport.AddDescriptor(q, cmdAddr + cmdLen, respLen, true, *cmdDesc);
        VALIDATE(respDesc, false, "Add resp descriptor failed");

        const VirtioCmdToken token = transport.BeginCmd(q, *cmdDesc);
        transport.EndCmd(token, true);

        if (!respType)
            return true;
        if (sl::NativePtr(cmdAddr + cmdLen + hhdmBase).As<volatile GpuCmdHeader>()->responseType == *respType)
            return true;

        Log("VirtioGpu::SendCmd(): Bad response type", LogLevel::Error);
        return false;
    }

    sl::Opt<sl::Vector2u> VirtioGpuDriver::GetDisplayInfo(uint32_t scanoutId)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuCmdHeader* cmd = cmdPage.As<volatile GpuCmdHeader>();
        cmd->cmdType = GpuCmdType::GetDisplayInfo;

        if (!SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuCmdHeader), sizeof(GpuResponseDisplayInfo)))
            return {};

        volatile GpuResponseDisplayInfo* info = cmdPage.As<volatile GpuResponseDisplayInfo>(sizeof(GpuCmdHeader));
        VALIDATE(info->header.responseType == GpuCmdRespType::OkDisplayInfo, {}, "Bad response type");
        VALIDATE(info->displays[scanoutId].enabled, {}, "Scanout not enabled");

        return sl::Vector2u{ info->displays[scanoutId].rect.width, info->displays[scanoutId].rect.height };
    }

    sl::Opt<uint32_t> VirtioGpuDriver::CreateResource2D(size_t width, size_t height, GpuFormat format)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuCreateResource2D* cmd = cmdPage.As<volatile GpuCreateResource2D>();
        cmd->header.cmdType = GpuCmdType::ResourceCreate2D;
        cmd->width = width;
        cmd->height = height;
        cmd->resourceId = __atomic_add_fetch(&nextRid, 1, __ATOMIC_ACQUIRE);
        cmd->format = format;

        if (!SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuCreateResource2D), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData))
            return {};

        return static_cast<uint32_t>(cmd->resourceId); //cast away volatile
    }

    bool VirtioGpuDriver::ResourceUnref(uint32_t resId)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuResourceUnref* cmd = cmdPage.As<volatile GpuResourceUnref>();
        cmd->header.cmdType = GpuCmdType::ResourceUnref;
        cmd->resourceId = resId;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuResourceUnref), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::SetScanout(uint32_t resId, uint32_t scanoutId, sl::UIntRect rect)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuSetScanout* cmd = cmdPage.As<volatile GpuSetScanout>();
        cmd->header.cmdType = GpuCmdType::SetScanout;
        cmd->scanoutId = scanoutId;
        cmd->resourceId = resId;
        cmd->rect.y = rect.left;
        cmd->rect.x = rect.top;
        cmd->rect.width = rect.width;
        cmd->rect.height = rect.height;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuSetScanout), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::FlushResource(uint32_t resId, sl::UIntRect rect)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuResourceFlush* cmd = cmdPage.As<volatile GpuResourceFlush>();
        cmd->header.cmdType = GpuCmdType::ResourceFlush;
        cmd->resourceId = resId;
        cmd->rect.y = rect.left;
        cmd->rect.x = rect.top;
        cmd->rect.width = rect.width;
        cmd->rect.height = rect.height;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuResourceFlush), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::TransferToHost2D(uint32_t resId, sl::UIntRect rect, uintptr_t offset)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuTransferToHost2D* cmd = cmdPage.As<volatile GpuTransferToHost2D>();
        cmd->header.cmdType = GpuCmdType::TransferToHost2D;
        cmd->resourceId = resId;
        cmd->offset = offset;
        cmd->rect.y = rect.left;
        cmd->rect.x = rect.top;
        cmd->rect.width = rect.width;
        cmd->rect.height = rect.height;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuTransferToHost2D), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::AttachBacking(uint32_t resId, const sl::Vector<GpuMemEntry>& memEntries)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuAttachBacking* cmd = cmdPage.As<volatile GpuAttachBacking>();
        cmd->header.cmdType = GpuCmdType::AttachBacking;
        cmd->numEntries = memEntries.Size();
        cmd->resourcedId = resId;

        volatile GpuMemEntry* entries = cmdPage.As<volatile GpuMemEntry>(sizeof(GpuAttachBacking));
        for (size_t i = 0; i < memEntries.Size(); i++)
        {
            entries[i].base = memEntries[i].base;
            entries[i].length = memEntries[i].length;
        }   

        const size_t cmdSize = sizeof(GpuAttachBacking) + (sizeof(GpuMemEntry) * memEntries.Size());
        return SendCmd(0, cmdPage.raw - hhdmBase, cmdSize, sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::DetachBacking(uint32_t resId)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuDetachBacking* cmd = cmdPage.As<volatile GpuDetachBacking>();
        cmd->header.cmdType = GpuCmdType::DetachBacking;
        cmd->resourceId = resId;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuDetachBacking), 
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::UpdateCursor(uint32_t resId, sl::Vector2u pos)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuUpdateCursor* cmd = cmdPage.As<volatile GpuUpdateCursor>();
        cmd->header.cmdType = GpuCmdType::UpdateCursor;
        cmd->cursorX = pos.x;
        cmd->cursorY = pos.y;
        cmd->hotX = 0;
        cmd->hotY = 0;
        cmd->resourceId = resId;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuUpdateCursor),
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::MoveCursor(sl::Vector2u pos)
    {
        sl::ScopedLock scopeLock(lock);
        volatile GpuUpdateCursor* cmd = cmdPage.As<volatile GpuUpdateCursor>();
        cmd->header.cmdType = GpuCmdType::MoveCursor;
        cmd->cursorX = pos.x;
        cmd->cursorY = pos.y;

        return SendCmd(0, cmdPage.raw - hhdmBase, sizeof(GpuUpdateCursor),
            sizeof(GpuCmdHeader), GpuCmdRespType::OkNoData);
    }

    bool VirtioGpuDriver::Init()
    {
        lock.Lock();
        nextRid = 0;

        transport.Reset(); //start by resetting the device
        VirtioStatus status = transport.ProgressInit(VirtioStatus::Acknowledge);
        VALIDATE((status & VirtioStatus::DeviceNeedReset) == VirtioStatus::None, false, "Acknowledge bit")

        //tell the device we know what it is, and that we're loading a driver.
        status = transport.ProgressInit(VirtioStatus::Driver);
        VALIDATE((status & VirtioStatus::DeviceNeedReset) == VirtioStatus::None, false, "Driver bit")

        Log("Virtio GPU features: 0x%x, 0x%x, 0x%x, 0x%x", LogLevel::Verbose, transport.FeaturesReadWrite(0), 
            transport.FeaturesReadWrite(1), transport.FeaturesReadWrite(2), transport.FeaturesReadWrite(3));

        //we dont negotiate any features with the gpu device.
        transport.FeaturesReadWrite(0, 0);
        status = transport.ProgressInit(VirtioStatus::FeaturesOk);
        VALIDATE((status & VirtioStatus::DeviceNeedReset) == VirtioStatus::None, false, "Features OK bit")
        VALIDATE((status & VirtioStatus::FeaturesOk) == VirtioStatus::FeaturesOk, false, "Features OK bit")
        
        //setup the various queues we want (0 = command queue, 1 = cursor queue)
        VALIDATE(transport.InitQueue(0, 64), false, "Command queue init failed.");
        VALIDATE(transport.InitQueue(1, 16), false, "Cursor queue init failed.");

        //tell the device the driver is happy, and ready to move to be fully operational.
        status = transport.ProgressInit(VirtioStatus::DriverOk);
        VALIDATE((status & VirtioStatus::DeviceNeedReset) == VirtioStatus::None, false, "Driver OK bit")
        //at this point, the device is now live and will begin processing commands.

        cmdPage = PMM::Global().Alloc() + hhdmBase;
        lock.Unlock();

        volatile VirtioGpuConfig* config = transport.GetDeviceConfig().As<volatile VirtioGpuConfig>();
        for (size_t i = 0; i < config->numScanouts; i++)
        {
            VirtioFramebuffer* fb = new VirtioFramebuffer(*this, i);
            auto maybeFbId = Devices::DeviceManager::Global().AttachDevice(fb);
            ASSERT(maybeFbId, "Virtio framebuffer attach failed");

            framebuffers.PushBack(fb);
            Log("Virtio GPU scanout %lu available as device %lu", LogLevel::Info, i, (*maybeFbId));
        }

        return true;
    }

    bool VirtioGpuDriver::Deinit()
    {
        ASSERT_UNREACHABLE(); //TODO: ensure all resources released, free each queue
    }

    bool VirtioFramebuffer::Init()
    {
        using namespace Devices;

        sl::ScopedLock scopeLock(lock);
        if (status != DeviceStatus::Offline)
            return false;
        status = DeviceStatus::Starting;

        //gather info about the scanout
        if (scanoutId != -1u)
        {
            auto maybeDisplay = driver.GetDisplayInfo(scanoutId);
            VALIDATE(maybeDisplay, false, "Virtio scanout has no display");
            currentMode.width = maybeDisplay->x;
            currentMode.height = maybeDisplay->y;
            currentMode.bpp = 32;
            stride = currentMode.width * 4;
            currentMode.format = ColourFormats::R8G8B8A8;
        }

        //create the host resource
        auto maybeResource = driver.CreateResource2D(currentMode.width, currentMode.height, GpuFormat::R8G8B8X8);
        VALIDATE(maybeResource, false, "Failed to create resource");
        resourceId = *maybeResource;

        //allocate physical memory for framebuffer, attach it to the host resource
        const uint32_t fbPages = sl::AlignUp(currentMode.width * currentMode.height * 4, PageSize) / PageSize;
        address = PMM::Global().Alloc(fbPages);
        sl::Vector<GpuMemEntry> backingMemory;
        backingMemory.PushBack({ address, fbPages * (uint32_t)PageSize, 0 });
        VALIDATE(driver.AttachBacking(resourceId, backingMemory), false, "AttachBacking() failed");

        if (scanoutId != 1u)
        {
            //tell host to use this framebuffer as the scanout framebuffer
            const sl::UIntRect scanoutRect { 0, 0, currentMode.width, currentMode.height };
            VALIDATE(driver.SetScanout(resourceId, scanoutId, scanoutRect), false, "SetScanout() failed");
        }

        Log("Virtio framebuffer mode: %lu x %lu, %lubpp.", LogLevel::Verbose, 
            currentMode.width, currentMode.height, currentMode.bpp);
        status = DeviceStatus::Online;
        return true;
    }

    bool VirtioFramebuffer::Deinit()
    {
        sl::ScopedLock scopeLock(lock);
        
        //the reverse of initializing a framebuffer
        if (scanoutId != -1u)
        {
            //if we're driving a scanout, disable that scanout by assigning resource 0 (null).
            const sl::UIntRect scanoutRect { 0, 0, currentMode.width, currentMode.height };
            VALIDATE(driver.SetScanout(0, scanoutId, scanoutRect), false, "SetScanout() failed");
        }

        //detach the backing pages from the resource, free them.
        VALIDATE(driver.DetachBacking(resourceId), false, "DetachBacking() failed");
        const uint32_t fbPages = sl::AlignUp(currentMode.width * currentMode.height * 4, PageSize) / PageSize;
        PMM::Global().Free(address, fbPages);
        address = 0;

        VALIDATE(driver.ResourceUnref(resourceId), false, "ResourceUnref() failed.");
        resourceId = 0;

        return true;
    }

    bool VirtioFramebuffer::CanModeset()
    {
        return true;
    }

    Devices::FramebufferMode VirtioFramebuffer::CurrentMode()
    {
        return currentMode;
    }

    bool VirtioFramebuffer::SetMode(const Devices::FramebufferMode& newMode)
    {
        VALIDATE(newMode.bpp == 32, false, "Virtio framebuffers require 32bpp");
        VALIDATE(newMode.format.redMask == 0xFF, false, "Masks must be 8 bits");
        VALIDATE(newMode.format.greenMask == 0xFF, false, "Masks must be 8 bits");
        VALIDATE(newMode.format.blueMask == 0xFF, false, "Masks must be 8 bits");

        ASSERT_UNREACHABLE(); //TODO:
    }

    sl::NativePtr VirtioFramebuffer::LinearAddress()
    {
        return address + hhdmBase;
    }

    void VirtioFramebuffer::BeginDraw()
    {} //no-op, we can safely write to framebuffer memory without side-effects.

    void VirtioFramebuffer::EndDraw()
    {
        sl::ScopedLock scopeLock(lock);
        const sl::UIntRect framebufferBounds = { 0, 0, currentMode.width, currentMode.height };
        driver.TransferToHost2D(resourceId, framebufferBounds, 0);
        driver.FlushResource(resourceId, framebufferBounds);
    }
}
