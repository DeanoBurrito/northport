#include <Gpu.h>
#include <Log.h>
#include <interfaces/driver/Memory.h>
#include <interfaces/driver/Drivers.h>
#include <NanoPrintf.h>

namespace Virtio
{
    sl::Opt<uint32_t> Gpu::AllocRid()
    {
        sl::ScopedLock scopelock(freeRidsLock);

        if (freeRids.Size() == 0)
            return {};

        const uint32_t rid = freeRids.Front().base;
        freeRids.Front().base++;
        freeRids.Front().length--;
        if (freeRids.Front().length == 0)
            freeRids.PopFront();

        return rid;
    }

    void Gpu::FreeRid(uint32_t id)
    {
        sl::ScopedLock scopelock(freeRidsLock);
        //TODO: FreeRid() impl
    }

    bool Gpu::DoCommand(void* cmd, size_t cmdLen, void* resp, size_t respLen)
    {
        npk_mdl mdls[2];
        if (!npk_vm_acquire_mdl(&mdls[0], cmd, cmdLen))
        {
            Log("Failed to acquire command MDL.", LogLevel::Error);
            return {};
        }
        if (!npk_vm_acquire_mdl(&mdls[1], resp, respLen))
        {
            npk_vm_release_mdl(cmd);
            Log("Failed to acquire response MDL.", LogLevel::Error);
            return {};
        }

        auto token = transport.PrepareCommand(0, mdls);
        if (token.HasValue() && !transport.BeginCommand(0, *token))
            token = {};
        if (token.HasValue() && !transport.EndCommand(0, *token))
            token = {};

        npk_vm_release_mdl(cmd);
        npk_vm_release_mdl(resp);
        return token.HasValue();
    }

    sl::Opt<GpuResponseDisplayInfo> Gpu::CmdGetDisplayInfo()
    {
        GpuCmdHeader command {};
        command.cmdType = GpuCmdType::GetDisplayInfo;

        GpuResponseDisplayInfo response {};
        VALIDATE_(DoCommand(&command, sizeof(command), &response, sizeof(response)), {});
        VALIDATE_(response.header.respType == GpuCmdRespType::OkDisplayInfo, {});

        return response;
    }

    sl::Opt<uint32_t> Gpu::CmdCreateResource2D(GpuFormat format, sl::Vector2u size)
    {
        auto rid = AllocRid();
        VALIDATE_(rid.Value(), {});

        GpuCreateResource2D command {};
        command.cmdType = GpuCmdType::CreateResource2d;
        command.format = static_cast<uint32_t>(format);
        command.width = size.x;
        command.height = size.y;
        command.resourceId = *rid;

        GpuCmdResponse response {};
        VALIDATE_(response.respType == GpuCmdRespType::OkNoData, {});

        return *rid;
    }

    bool Gpu::CmdResourceUnref(uint32_t resourceId)
    {}

    bool Gpu::CmdSetScanout(uint32_t scanoutId, uint32_t fbResourceId)
    {}

    bool Gpu::CmdResourceFlush(uint32_t resourceId, sl::UIntRect rect)
    {}

    bool Gpu::CmdTransferToHost2d(uint32_t resourceId, sl::UIntRect rect, uint64_t offset)
    {}

    bool Gpu::CmdAttachBacking(uint32_t resourceId, sl::Span<GpuMemEntry> memory)
    {}

    bool Gpu::CmdDetachBacking(uint32_t resourceId)
    {}

    void Gpu::AddScanout(uint32_t id, GpuDisplay data)
    {
        scanoutFramebufferLock.WriterLock();
        auto& scanout = scanouts.EmplaceAt(id);
        scanout.attachedFb = NoRid;
        scanout.id = id;
        scanout.rect.width = data.rect.width;
        scanout.rect.height = data.rect.height;
        scanout.rect.left = data.rect.x;
        scanout.rect.top = data.rect.y;
        scanoutFramebufferLock.WriterUnlock();

        Log("Gpu added scanout %" PRIu32", w=%lu, h=%lu, x=%lu, y=%lu",
            LogLevel::Info, id, scanout.rect.width, scanout.rect.height,
            scanout.rect.left, scanout.rect.top);
    }

    void Gpu::UpdateScanout(uint32_t id, GpuDisplay newData)
    {
        ASSERT_UNREACHABLE();
    }

    void Gpu::RemoveScanout(uint32_t id)
    {
        ASSERT_UNREACHABLE();
    }

    void Gpu::ScanoutsUpdated()
    {
        auto displayInfo = CmdGetDisplayInfo();
        VALIDATE_(displayInfo.HasValue(), );

        auto cfg = transport.DeviceConfig().As<const volatile GpuConfig>();
        const size_t numScanouts = cfg->numScanouts;

        for (size_t i = 0; i < numScanouts; i++)
        {
            const auto disp = displayInfo->displays[i];

            if (i >= scanouts.Size() || scanouts[i].rect.width == 0)
            {
                AddScanout(i, disp);
                continue;
            }

            if (scanouts[i].rect.width != disp.rect.width ||
                scanouts[i].rect.height != disp.rect.height ||
                scanouts[i].rect.left != disp.rect.x || 
                scanouts[i].rect.top != disp.rect.y)
            {
                UpdateScanout(i, disp);
                continue;
            }

            if (scanouts[i].rect.width != 0 && disp.enabled.Load() == 0)
                RemoveScanout(i);
        }

        for (size_t i = numScanouts; i < scanouts.Size(); i++)
            RemoveScanout(i);

        RegenSummary();
    }

    void Gpu::RegenSummary()
    {
        constexpr const char FormatStr[] = "virtio gpu: %zu scanouts, %zu framebuffers";
        
        scanoutFramebufferLock.ReaderLock();
        const size_t scanoutCount = scanouts.Size();
        const size_t fbCount = framebuffers.Size();
        scanoutFramebufferLock.ReaderUnlock();

        const size_t len = npf_snprintf(nullptr, 0, FormatStr, scanoutCount, fbCount) + 1;
        char* buff = static_cast<char*>(npk_heap_alloc(len));
        npf_snprintf(buff, len, FormatStr, scanoutCount, fbCount);

        summaryStringLock.WriterLock();
        const npk_string oldStr = summaryString;
        summaryString.length = len;
        summaryString.data = buff;
        summaryStringLock.WriterUnlock();

        if (oldStr.data != nullptr)
            npk_heap_free((void*)oldStr.data, oldStr.length);
    }

    bool Gpu::Init()
    {
        freeRids.PushBack({ 1, -1u - 1 });

        //we dont need to negotiate any gpu features for now
        for (size_t i = 0; i < 4; i++)
            transport.Features(i, 0);
        VALIDATE_(transport.ProgressInit(InitPhase::FeaturesOK), false);

        //setup the queues we want: q0 is the command queue, q1 is the cursor queue
        VALIDATE_(transport.CreateQueue(0, 64), false);
        VALIDATE_(transport.CreateQueue(1, 64), false);

        //we're done with device setup, complete virtio init
        VALIDATE_(transport.ProgressInit(InitPhase::DriverOk), false);
        //TODO: a way for gpu to notify kernel of scanout changes
        ScanoutsUpdated(); //prompts the driver to check for 'newly' connected outputs

        kernelApi.header.type = npk_device_api_type::Gpu;
        kernelApi.header.driver_data = this;
        kernelApi.header.get_summary = ApiGetSummary;
        kernelApi.create_framebuffer = ApiCreateFramebuffer;
        kernelApi.destroy_framebuffer = ApiDestroyFramebuffer;
        kernelApi.get_scanout_info = ApiGetScanoutInfo;
        kernelApi.set_scanout_framebuffer = ApiSetScanoutFramebuffer;
        VALIDATE_(npk_add_device_api(&kernelApi.header), false); 

        return true;
    }

    npk_string Gpu::GetSummaryString()
    {
        summaryStringLock.ReaderLock();
        npk_string ret = summaryString;
        summaryStringLock.ReaderUnlock();

        return ret;
    };

    npk_handle Gpu::CreateFramebuffer(sl::Vector2u size, npk_pixel_format format)
    {}

    bool Gpu::SetScanout(size_t scanoutId, npk_framebuffer_device_api* fb)
    {}

    npk_string ApiGetSummary(npk_device_api* api)
    { return static_cast<Gpu*>(api->driver_data)->GetSummaryString(); }

    npk_handle ApiCreateFramebuffer(npk_device_api* api, size_t x, size_t y, npk_pixel_format format)
    { return static_cast<Gpu*>(api->driver_data)->CreateFramebuffer({ x, y }, format); }

    bool ApiDestroyFramebuffer(npk_device_api* api, npk_handle fb)
    { return false; } //TODO: 

    bool ApiSetScanoutFramebuffer(npk_device_api* api, npk_handle scanout_index, npk_framebuffer_device_api* fb)
    { return static_cast<Gpu*>(api->driver_data)->SetScanout(scanout_index, fb); }

    size_t ApiGetScanoutInfo(npk_device_api* api, REQUIRED npk_scanout_info* buff, size_t buff_count, size_t first)
    { ASSERT_UNREACHABLE(); } //TODO:
}
