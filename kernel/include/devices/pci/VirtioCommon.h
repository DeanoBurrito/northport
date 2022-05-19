#pragma once

#include <stdint.h>
#include <stddef.h>
#include <BufferView.h>
#include <devices/pci/PciEndpoints.h>
#include <Optional.h>

namespace Kernel::Devices::Pci
{
    enum class DeviceStatus : uint8_t
    {
        None = 0,

        Acknowledge = (1 << 0),
        Driver = (1 << 1),
        DriverOk = (1 << 2),
        FeaturesOk = (1 << 3),
        DeviceNeedsReset = (1 << 6),
        Failed = (1 << 7),
    };

    enum class DeviceFeatures : uint64_t
    {
        //support for indirect descriptors
        RingIndirectDescriptors = (1ul << 28),
        //support for usedEvent/availEvent fields
        RingEventIndex = (1ul << 29),
        //if set, indicates a device is non-legacy
        Version = (1ul << 32),
        AccessPlatform = (1ul << 33),
        //indicates support for packed virtqueue layout
        RingPacked = (1ul << 34),
        //support for returning buffers in order they were consumed
        InOrder = (1ul << 35),
        //setting this means devices and drivers will use strict-memory ordering: for when quest != host cpu.
        OrderPlatform = (1ul << 36),
        //ssupport for single root io virtualization.
        SrIov = (1ul << 37),
        //indicates that a device passes extra data in it's notifications
        NotificationData = (1ul << 38),
    };

    enum class VirtQueueFlags : uint16_t
    {
        None = 0,

        //means next pointer is valid
        HasNext = (1 << 0),
        //implied to be readonly if not set
        DeviceWriteOnly = (1 << 1),
        //means this buffer contains a list of descriptors. Check for device support first.
        Indirect = (1 << 2),
    };

    struct [[gnu::packed]] VirtQueueDescriptor
    {
        uint64_t bufferAddr; //physical address in guest space
        uint32_t length;
        VirtQueueFlags flags;
        uint16_t next;
    };

    enum class VirtQueueAvailFlags : uint16_t
    {
        None = 0,
        
        //suppress notifications when this buffer is used.
        NoInterrupt = (1 << 0),
    };

    struct [[gnu::packed]] VirtQueueAvailable
    {
        VirtQueueAvailFlags flags;
        uint16_t index;
        uint16_t ring[];
    };

    struct VirtQueueUsedElem
    {
        uint32_t id; //index of first descriptor
        uint32_t length; //length of the descriptor chain
    };

    enum class VirtQueueUsedFlags : uint16_t
    {
        None = 0,

        //suppress notifications from the driver when we add an available buffer
        NoNotify = (1 << 0),
    };

    struct [[gnu::packed]] VirtQueueUsed
    {
        VirtQueueUsedFlags flags;
        uint16_t index;
        VirtQueueUsedElem ring[];
    };

    enum class VirtioPciCapType : uint8_t
    {
        CommonConfig = 1,
        NotifyConfig = 2,
        IsrConfig = 3,
        DeviceConfig = 4,
        PciConfig = 5,
    };

    struct [[gnu::packed]] VirtioPciCap : public PciCap
    {
        uint8_t capLength;
        VirtioPciCapType configType;
        uint8_t bar; //index, which bar to use for this config
        uint8_t reserved[3];
        uint32_t offset; //byte offset within bar
        uint32_t barLength; //length within bar
    };

    struct [[gnu::packed]] VirtioPciCommonConfig
    {
        uint32_t deviceFeatureSelect; //used for selecting which bank of 32 flags deviceFeature shows.
        uint32_t deviceFeature; //readonly for driver
        uint32_t driverFeatureSelect; 
        uint32_t driverFeature;
        uint16_t msixConfig; //used to set config vector for msix
        uint16_t numberOfQueues; //readonly for driver, max queues available
        uint8_t deviceStatus; //write zero to reset device, shows status otherwise
        uint8_t configGeneration; //readonly for driver

        //for getting info about virt queues
        uint16_t queueSelect; //queue index to change meaning of following fields
        uint16_t queueSize; //on reset, maximium queue size this device supports, zero means unavailable. Can be reduced by software.
        uint16_t queueMsixVector;
        uint16_t queueEnable; //non-zero means device will execute requets from this queue.
        uint16_t queueNotifyOff; //index of this queue into the notification structure.
        uint64_t queueDesc; //physical address of descriptor/table area here
        uint64_t queueDriver; //physical address of driver/available area here
        uint64_t queueDevice; //physical address of device/used area here
    };

    struct [[gnu::packed]] VirtioPciNotifyConfig : public VirtioPciCap
    {
        uint32_t notifyOffsetMultiplier;
    };

    sl::Opt<VirtioPciCommonConfig*> GetVirtioPciCommonConfig(PciAddress addr);
}
