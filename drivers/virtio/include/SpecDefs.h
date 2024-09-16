#pragma once

#include <stdint.h>
#include <Endian.h>

namespace Virtio
{
    constexpr uint8_t CommonConfigCap = 1;
    constexpr uint8_t NotifyConfigCap = 2;
    constexpr uint8_t DeviceConfigCap = 4;
    
    struct PciCommonCfg
    {
        sl::Le32 deviceFeatureSelect;
        sl::Le32 deviceFeatures;
        sl::Le32 driverFeatureSelect;
        sl::Le32 driverFeatures;

        sl::Le16 msixConfig;
        sl::Le16 numQueues;
        sl::Le8 status;
        sl::Le8 cfgGeneration;

        sl::Le16 queueSelect;
        sl::Le16 queueSize;
        sl::Le16 queueMsixVector;
        sl::Le16 queueEnable;
        sl::Le16 queueNotifyOffset;
        sl::Le64 queueDesc;
        sl::Le64 queueDriver;
        sl::Le64 queueDevice;
    };

    enum class MmioReg : unsigned
    {
        Magic = 0,
        Version = 0x4,
        DeviceId = 0x8,
        VendorId = 0xC,
        DeviceFeatures = 0x10,
        DeviceFeaturesSelect = 0x14,
        DriverFeatures = 0x20,
        DriverFeaturesSelect = 0x24,
        DriverPageSize = 0x28,

        QueueSelect = 0x30,
        QueueSizeMax = 0x34,
        QueueSize = 0x38,
        QueueAlign = 0x3C,
        QueuePfn = 0x40,
        QueueReady = 0x44,

        QueueNotify = 0x50,

        InterruptStatus = 0x60,
        InterruptAck = 0x64,

        Status = 0x70,

        QueueDescLow = 0x80,
        QueueDescHigh = 0x84,
        QueueDriverLow = 0x90,
        QueueDriverHigh = 0x94,
        QueueDeviceLow = 0xA0,
        QueueDeviceHigh = 0xA4,

        DeviceConfigBase = 0x100,
    };

    enum class VirtqDescFlags : uint16_t
    {
        None = 0,
        Next = 1 << 0,
        Write = 1 << 1,
        Indirect = 1 << 2,
    };

    constexpr VirtqDescFlags operator&(const VirtqDescFlags& a, const VirtqDescFlags& b)
    { return (VirtqDescFlags)((uintptr_t)a & (uintptr_t)b); }

    struct VirtqDescEntry
    {
        sl::Le64 base;
        sl::Le32 length;
        sl::Le16 flags;
        sl::Le16 next;
    };

    enum class VirtqAvailFlags : uint16_t
    {
        None = 0,
        NoInterrupt = 1 << 0,
    };

    struct VirtqAvailable
    {
        sl::Le16 flags;
        sl::Le16 index;
        sl::Le16 ring[];
    };

    enum class VirtqUsedFlags : uint16_t
    {
        None = 0,
        NoNotify = 1 << 0,
    };

    struct VirtqUsedElement
    {
        sl::Le32 index;
        sl::Le32 length;
    };

    struct VirtqUsed
    {
        sl::Le16 flags;
        sl::Le16 index;
        VirtqUsedElement ring[];
    };

    struct GpuConfig
    {
        sl::Le32 eventsRead;
        sl::Le32 eventsClear;
        sl::Le32 numScanouts;
        sl::Le32 numCapsets;
    };

    enum class GpuCmdType : uint32_t
    {
        //2d commands
        GetDisplayInfo = 0x100,
        CreateResource2d,
        ResoruceUnref,
        SetScanout,
        ResourceFlush,
        TransferToHost2D,
        AttachBacking,
        DetachBacking,

        //cursor commands
        UpdateCursor = 0x300,
        MoveCursor,
    };

    enum class GpuCmdRespType : uint32_t
    {
        OkNoData = 0x1100,
        OkDisplayInfo,

        ErrUnspec = 0x1200,
        ErrOutOfMemory,
        ErrInvalidScanoutId,
        ErrInvalidResourceId,
        ErrInvalidContextId,
        ErrInvalidParameter,
    };

    struct GpuCmdHeader
    {
        union
        {
            GpuCmdType cmdType;
            GpuCmdRespType respType;
        };
        sl::Le32 flags;
        sl::Le64 fenceId;
        sl::Le32 vcontextId;
        sl::Le8 ringIndex;
        sl::Le8 reserved[3];
    };

    using GpuCmdResponse = GpuCmdHeader;

    struct GpuRect
    {
        sl::Le32 x;
        sl::Le32 y;
        sl::Le32 width;
        sl::Le32 height;
    };

    struct GpuDisplay
    {
        GpuRect rect;
        sl::Le32 enabled;
        sl::Le32 flags;
    };

    struct GpuResponseDisplayInfo
    {
        GpuCmdHeader header;
        GpuDisplay displays[16]; //max number of scanouts, from the spec
    };

    enum class GpuFormat : uint32_t
    {
        B8G8R8A8 = 1,
        B8G8R8X8 = 2,
        A8R8G8B8 = 3,
        X8R8G8B8 = 4,
        R8G8B8A8 = 67,
        X8B8G8R8 = 68,
        A8B8G8R8 = 121,
        R8G8B8X8 = 134,
    };

    struct GpuCreateResource2D : public GpuCmdHeader
    {
        sl::Le32 resourceId;
        sl::Le32 format;
        sl::Le32 width;
        sl::Le32 height;
    };

    struct GpuResourceUnref : public GpuCmdHeader
    {
        sl::Le32 resourceId;
        sl::Le32 padding;
    };

    struct GpuSetScanout : public GpuCmdHeader
    {
        GpuRect rect;
        sl::Le32 scanoutId;
        sl::Le32 resourceId;
    };

    struct GpuResourceFlush : public GpuCmdHeader
    {
        GpuRect rect;
        sl::Le32 resourceId;
        sl::Le32 padding;
    };

    struct GpuTransferToHost2d : public GpuCmdHeader
    {
        GpuRect rect;
        sl::Le64 offset;
        sl::Le32 resourceId;
        sl::Le32 padding;
    };

    struct GpuMemEntry
    {
        sl::Le64 addr;
        sl::Le32 length;
        sl::Le32 padding;
    };

    struct GpuResourceAttachBacking : public GpuCmdHeader
    {
        sl::Le32 resourceId;
        sl::Le32 entries;
    };

    struct GpuResourceDetachBacking : public GpuCmdHeader
    {
        sl::Le32 resourceId;
        sl::Le32 padding;
    };
}
