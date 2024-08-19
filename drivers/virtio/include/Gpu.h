#pragma once

#include <SpecDefs.h>
#include <Transport.h>
#include <interfaces/driver/Drivers.h>
#include <containers/LinkedList.h>
#include <containers/Vector.h>
#include <Locks.h>
#include <Rects.h>

namespace Virtio
{
    constexpr uint32_t NoRid = -1;

    struct GpuScanout
    {
        sl::UIntRect rect;
        uint32_t id;
        uint32_t attachedFb;
    };

    struct GpuFramebuffer
    {
        uint32_t rid;
        uint32_t scanout;
        sl::Vector2u size;
    };

    class Gpu
    {
    private:
        struct FreeRidRange
        {
            uint32_t base;
            uint32_t length;
        };

        sl::LinkedList<FreeRidRange> freeRids;
        sl::SpinLock freeRidsLock;

        sl::RwLock summaryStringLock;
        npk_string summaryString;

        sl::RwLock scanoutFramebufferLock;
        sl::Vector<GpuScanout> scanouts;
        sl::LinkedList<GpuFramebuffer> framebuffers;

        npk_gpu_device_api kernelApi;

        sl::Opt<uint32_t> AllocRid();
        void FreeRid(uint32_t id);

        bool DoCommand(void* cmd, size_t cmdLen, void* resp, size_t respLen);
        sl::Opt<GpuResponseDisplayInfo> CmdGetDisplayInfo();
        sl::Opt<uint32_t> CmdCreateResource2D(GpuFormat format, sl::Vector2u size);
        bool CmdResourceUnref(uint32_t resourceId);
        bool CmdSetScanout(uint32_t scanoutId, uint32_t fbResourceId);
        bool CmdResourceFlush(uint32_t resourceId, sl::UIntRect rect);
        bool CmdTransferToHost2d(uint32_t resourceId, sl::UIntRect rect, uint64_t offset);
        bool CmdAttachBacking(uint32_t resourceId, sl::Span<GpuMemEntry> memory);
        bool CmdDetachBacking(uint32_t resourceId);

        void AddScanout(uint32_t id, GpuDisplay data);
        void UpdateScanout(uint32_t id, GpuDisplay newData);
        void RemoveScanout(uint32_t id);
        void ScanoutsUpdated();

        void RegenSummary();

    public:
        Transport transport;

        bool Init();
        npk_string GetSummaryString();
        npk_handle CreateFramebuffer(sl::Vector2u size, npk_pixel_format format);
        bool SetScanout(size_t scanoutId, npk_framebuffer_device_api* fb);
    };

    npk_string ApiGetSummary(npk_device_api* api);
    npk_handle ApiCreateFramebuffer(npk_device_api* api, size_t x, size_t y, npk_pixel_format format);
    bool ApiDestroyFramebuffer(npk_device_api* api, npk_handle fb);
    bool ApiSetScanoutFramebuffer(npk_device_api* api, npk_handle scanout_index, npk_framebuffer_device_api* fb);
    size_t ApiGetScanoutInfo(npk_device_api* api, REQUIRED npk_scanout_info* buff, size_t buff_count, size_t first);
}
