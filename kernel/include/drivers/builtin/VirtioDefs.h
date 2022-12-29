#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npk::Drivers
{
    constexpr uint8_t PciCapTypeCommonCfg = 1;
    constexpr uint8_t PciCapTypeNotifyCfg = 2;
    constexpr uint8_t PciCapTypeDeviceCfg = 4;
    
    struct VirtioPciCommonCfg
    {
        //global device details
        uint32_t deviceFeatureSelect;
        uint32_t deviceFeatures;
        uint32_t driverFeatureSelect;
        uint32_t driverFeatures;

        uint16_t msixConfig;
        uint16_t numQueues;
        uint8_t deviceStatus;
        uint8_t configGeneration; //atomic, updated by DEVICE whenever any fields are modified.
        
        //specific virtqueue details
        //change which virtqueue these refer to by setting queueSelect.
        uint16_t queueSelect;
        uint16_t queueSize;
        uint16_t queueMsixVector;
        uint16_t queueEnable;
        uint16_t queueNotifyOffset;
        uint64_t queueDesc;
        uint64_t queueDriver;
        uint64_t queueDevice;
    };

    constexpr uint32_t VirtioMmioMagic = 0x74726976;

    enum class VirtioReg : size_t
    {
        Magic = 0,
        Version = 0x4,
        DeviceId = 0x8,
        VendorId = 0xC,
        DeviceFeatures = 0x10,
        DeviceFeaturesSel = 0x14,
        DriverFeatures = 0x20,
        DriverFeaturesSel = 0x24,
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

    struct VirtqDescriptor
    {
        uint64_t base;
        uint32_t length;
        VirtqDescFlags flags;
        uint16_t next;
    };

    enum class VirtqAvailFlags : uint16_t
    {
        None = 0,
        NoInterrupt = 1 << 0
    };
    
    struct VirtqAvail
    {
        VirtqAvailFlags flags;
        uint16_t index;
        uint16_t ring[];
    };

    enum class VirtqUsedFlags : uint16_t
    {
        None = 0,
        NoNotify = 1 << 0
    };

    struct VirtqUsedElement
    {
        uint32_t index;
        uint32_t length;
    };

    struct VirtqUsed
    {
        VirtqUsedFlags flags;
        uint16_t index;
        VirtqUsedElement ring[];
    };

    struct VirtioGpuConfig
    {
        uint32_t eventsRead;
        uint32_t eventsClear;
        uint32_t numScanouts;
        uint32_t numCapsets;
    };

    enum class GpuCmdType : uint32_t
    {
        //2D commands
        GetDisplayInfo = 0x100,
        ResourceCreate2D,
        ResourceUnref,
        SetScanout,
        ResourceFlush,
        TransferToHost2D,
        AttachBacking,
        DetachBacking,
        GetCapsetInfo,
        GetCapset,
        GetEdid,
        ResourceAssignUuid,
        ResourceCreateBlob,
        SetScanoutBlob,

        //Cursor commands
        UpdateCursor = 0x300,
        MoveCursor,
    };

    enum class GpuCmdResponseType : uint32_t
    {
        //Great success!
        OkNoData = 0x1100,
        OkDisplayInfo,
        OkCapsetInfo,
        OkCapset,
        OkEdid,
        OkResourceUuid,
        OkMapInfo,

        //Error responses
        ErrUnspec = 0x1200,
        ErrOutOfMemory,
        ErrInvalidScanoutId,
        ErrInvalidResourceId,
        ErrInvalidContextId,
        ErrInvalidParameter,
    };

    enum class GpuCmdFlags : uint32_t
    {
        None = 0,
    };

    constexpr GpuCmdFlags operator&(const GpuCmdFlags& a, const GpuCmdFlags& b)
    { return (GpuCmdFlags)((uintptr_t)a & (uintptr_t)b); }

    struct GpuCmdHeader
    {
        union
        {
            GpuCmdType cmdType;
            GpuCmdResponseType responseType;
        };
        GpuCmdFlags flags = GpuCmdFlags::None;
        uint64_t fenceId;
        uint32_t ctxId;
        uint8_t ringIdx;
        uint8_t reserved[3];
    };

    struct VirtioGpuRect
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    };

    struct GpuResponseDisplayInfo
    {
        GpuCmdHeader header;
        struct GpuDisplay 
        {
            VirtioGpuRect rect;
            uint32_t enabled;
            uint32_t flags;
        } displays[16]; //maximum number of scanouts
    };

    struct GpuGetEdid
    {
        GpuCmdHeader header;
        uint32_t scanout;
        uint32_t reserved;
    };

    struct GpuResponseEdid
    {
        GpuCmdHeader header;
        uint32_t size;
        uint32_t reserved;
        uint8_t edid[1024];
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

    struct GpuCreateResource2D
    {
        GpuCmdHeader header;
        uint32_t resourceId;
        GpuFormat format;
        uint32_t width;
        uint32_t height;
    };

    struct GpuResourceUnref
    {
        GpuCmdHeader header;
        uint32_t resourceId;
        uint32_t reserved;
    };

    struct GpuSetScanout
    {
        GpuCmdHeader header;
        VirtioGpuRect rect;
        uint32_t scanoutId;
        uint32_t resourceId;
    };

    struct GpuResourceFlush
    {
        GpuCmdHeader header;
        VirtioGpuRect rect;
        uint32_t resourceId;
        uint32_t reserved;
    };

    struct GpuTransferToHost2D
    {
        GpuCmdHeader header;
        VirtioGpuRect rect;
        uint64_t offset;
        uint32_t resourceId;
        uint32_t reserved;
    };

    struct GpuAttachBacking
    {
        GpuCmdHeader header;
        uint32_t resourcedId;
        uint32_t numEntries;
    };

    struct GpuMemEntry
    {
        uint64_t base;
        uint32_t length;
        uint32_t reserved;
    };

    struct GpuDetachBacking
    {
        GpuCmdHeader header;
        uint32_t resourceId;
        uint32_t reserved;
    };

    struct GpuUpdateCursor
    {
        GpuCmdHeader header;
        uint32_t scanoutId;
        uint32_t cursorX;
        uint32_t cursorY;
        uint32_t reserved0;
        uint32_t resourceId;
        uint32_t hotX;
        uint32_t hotY;
        uint32_t reserved1;
    };
}
