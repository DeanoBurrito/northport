#pragma once

#include <devices/GenericDevices.h>
#include <drivers/builtin/VirtioTransport.h>
#include <drivers/builtin/VirtioDefs.h>
#include <containers/Vector.h>
#include <Rects.h>

namespace Npk::Drivers
{
    void VirtioGpuMain(void* arg);

    struct VirtioGpuConfig;
    class VirtioFramebuffer;

    class VirtioGpuDriver
    {
    friend VirtioFramebuffer;
    private:
        sl::TicketLock lock;
        sl::NativePtr cmdPage; //TODO: this is awful, create something like vmem.
        uint32_t nextRid;

        bool SendCmd(size_t q, uintptr_t cmdAddr, size_t cmdLen, size_t respLen, sl::Opt<GpuCmdRespType> respType = {});
        sl::Opt<sl::Vector2u> GetDisplayInfo(uint32_t scanoutId);
        sl::Opt<uint32_t> CreateResource2D(size_t width, size_t height, GpuFormat format);
        bool DestroyResource(uint32_t resId);
        bool SetScanout(uint32_t resId, uint32_t scanoutId, sl::UIntRect rect);
        bool FlushResource(uint32_t resId, sl::UIntRect rect);
        bool TransferToHost2D(uint32_t resId, sl::UIntRect rect, uintptr_t offset);
        bool AttachBacking(uint32_t resId, const sl::Vector<GpuMemEntry>& memEntries);
        bool DetachBacking(uint32_t resId);
        bool UpdateCursor(uint32_t resId, sl::Vector2u pos);
        bool MoveCursor(sl::Vector2u pos);

    public:
        VirtioTransport transport;
        sl::Vector<VirtioFramebuffer*> framebuffers;

        bool Init();
    };

    class VirtioFramebuffer : public Devices::GenericFramebuffer
    {
    friend VirtioGpuDriver;
    private:
        VirtioGpuDriver& driver;
        uint32_t scanoutId; //-1 for a detached framebuffer
        uint32_t resourceId;

        uintptr_t address;
        size_t stride;
        Devices::FramebufferMode currentMode;

        VirtioFramebuffer(VirtioGpuDriver& driv, size_t scanoutId = -1ul) : driver(driv), scanoutId(scanoutId)
        {}

    public:
        bool Init() override;
        bool Deinit() override;

        bool CanModeset() override;
        Devices::FramebufferMode CurrentMode() override;
        bool SetMode(const Devices::FramebufferMode& newMode) override;

        sl::NativePtr LinearAddress() override;
        void BeginDraw() override;
        void EndDrawAndFlush() override;
    };
}
