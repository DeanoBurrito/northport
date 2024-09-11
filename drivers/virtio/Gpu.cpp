#include <Gpu.h>
#include <Log.h>
#include <interfaces/driver/Memory.h>
#include <interfaces/driver/Drivers.h>
#include <NanoPrintf.h>

namespace Virtio
{
    static sl::Opt<GpuFormat> NpkFormatToVirtio(npk_pixel_format format)
    {
        if (format.mask_r != 0xFF || format.mask_g != 0xFF || format.mask_b != 0xFF)
        {
            Log("Pixel format must have exactly 8 bits per colour channel.", LogLevel::Error);
            return {};
        }
        if (format.mask_a != 0 && format.mask_a != 0xFF)
        {
            Log("Pixel format must have 0 or 8 bits for the alpha channel.", LogLevel::Error);
            return {};
        }

        if (format.shift_r == 0 && format.shift_g == 8 && format.shift_b == 16)
            return format.mask_a == 0xFF ? GpuFormat::R8G8B8A8 : GpuFormat::R8G8B8X8;
        if (format.shift_r == 8 && format.shift_g == 16 && format.shift_b == 24)
            return format.mask_a == 0xFF ? GpuFormat::A8R8G8B8 : GpuFormat::X8R8G8B8;
        if (format.shift_b == 0 && format.shift_g == 8 && format.shift_r == 16)
            return format.mask_a == 0xFF ? GpuFormat::B8G8R8A8 : GpuFormat::B8G8R8X8;
        if (format.shift_b == 8 && format.shift_g == 16 && format.shift_r == 24)
            return format.mask_a == 0xFF ? GpuFormat::A8B8G8R8 : GpuFormat::X8B8G8R8;

        return {};
    }

    static const char* GpuFormatToStr(GpuFormat format)
    {
        switch (format)
        {
        case GpuFormat::B8G8R8A8: return "b8g8r8a8";
        case GpuFormat::B8G8R8X8: return "b8g8r8x8";
        case GpuFormat::A8R8G8B8: return "a8r8g8b8";
        case GpuFormat::X8R8G8B8: return "x8r8g8b8";
        case GpuFormat::R8G8B8A8: return "r8g8b8a8";
        case GpuFormat::X8B8G8R8: return "x8b8g8r8";
        case GpuFormat::A8B8G8R8: return "a8b8g8r8";
        case GpuFormat::R8G8B8X8: return "r8g8b8x8";
        default: return "unknown format";
        }
    }

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
        CommandHandle handle {};
        handle.queueIndex = 0;
        handle.descId = *token;

        if (token.HasValue() && !transport.BeginCommand(handle))
            token = {};
        if (token.HasValue() && !transport.EndCommand(handle, -1))
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
        VALIDATE_(DoCommand(&command, sizeof(command), &response, sizeof(response)), {});
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
    {
        auto maybeFormat = NpkFormatToVirtio(format);
        VALIDATE(maybeFormat.HasValue(), NPK_INVALID_HANDLE, "Framebuffer creation failed, invalid pixel format.");

        auto maybeRes = CmdCreateResource2D(*maybeFormat, size);
        VALIDATE(maybeRes, NPK_INVALID_HANDLE, "Framebuffer creation failed, CreateResource2d() error.");

        GpuFramebuffer* fb = new GpuFramebuffer();
        if (fb == nullptr)
        {
            Log("Framebuffer creation failed, malloc() failure", LogLevel::Error);

            delete fb;
            CmdResourceUnref(*maybeRes);
            return NPK_INVALID_HANDLE;
        }

        fb->gpu = this;
        fb->size = size;
        fb->rid = *maybeRes;
        fb->scanout = NoRid;
        fb->format = *maybeFormat;

        fb->npkApi.header.type = npk_device_api_type::Framebuffer;
        fb->npkApi.header.driver_data = fb;
        fb->npkApi.header.get_summary = ApiGetFbSummary;
        fb->npkApi.header.get_summary = nullptr;
        fb->npkApi.get_mode = ApiGetMode;
        fb->npkApi.set_mode = nullptr; //TODO: modesetting for virtio framebuffers
        fb->npkApi.begin_draw = nullptr;
        fb->npkApi.end_draw = ApiEndDraw;

        if (!npk_add_device_api(&fb->npkApi.header))
        {
            Log("Framebuffer creation failed, npk_add_device_api() failure", LogLevel::Error);

            delete fb;
            CmdResourceUnref(*maybeRes);
            return NPK_INVALID_HANDLE;
        }

        scanoutFramebufferLock.WriterLock();
        framebuffers.EmplaceAt(fb->rid, fb);
        scanoutFramebufferLock.WriterUnlock();

        Log("Framebuffer %u created: w=%lu, h=%lu, format=%s, handle=%zu", LogLevel::Info,
            fb->rid, fb->size.x, fb->size.y, GpuFormatToStr(fb->format), fb->npkApi.header.id);
        return fb->npkApi.header.id;
    }

    bool Gpu::SetScanout(size_t scanoutId, npk_framebuffer_device_api* fb)
    { ASSERT_UNREACHABLE(); }

    npk_string ApiGetFbSummary(npk_device_api* api)
    { ASSERT_UNREACHABLE(); }

    npk_framebuffer_mode ApiGetMode(npk_device_api* api)
    { ASSERT_UNREACHABLE(); }

    void ApiEndDraw(npk_device_api* api, size_t x, size_t y, size_t w, size_t h)
    { ASSERT_UNREACHABLE(); }

    npk_string ApiGetSummary(npk_device_api* api)
    { return static_cast<Gpu*>(api->driver_data)->GetSummaryString(); }

    npk_handle ApiCreateFramebuffer(npk_device_api* api, size_t x, size_t y, npk_pixel_format format)
    { return static_cast<Gpu*>(api->driver_data)->CreateFramebuffer({ x, y }, format); }

    bool ApiDestroyFramebuffer(npk_device_api* api, npk_handle fb)
    { return false; } //TODO: 

    bool ApiSetScanoutFramebuffer(npk_device_api* api, npk_handle scanout_index, npk_framebuffer_device_api* fb)
    { return static_cast<Gpu*>(api->driver_data)->SetScanout(scanout_index, fb); }

    size_t ApiGetScanoutInfo(npk_device_api* api, REQUIRED npk_scanout_info* buff, size_t buff_count, size_t first)
    { ASSERT_UNREACHABLE(); }
}
