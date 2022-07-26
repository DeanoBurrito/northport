#pragma once

#include <NativePtr.h>
#include <containers/Vector.h>

namespace Kernel::Devices::Pci
{
    //For NVME-over-PCI, this is the structure pointed to by BAR0.
    struct [[gnu::packed]] NvmePropertyMap
    {
        uint64_t capabilities;
        uint32_t version; //31:16 = major, 15:8 = minor, 7:0 tertiary (revision)
        uint32_t interruptMaskSet;
        uint32_t interruptMaskClear; //IMC & IMS are disallowed if we're using msi-x
        uint32_t controllerConfig;
        uint8_t reserved0[4];
        uint32_t controllerStatus;
        uint32_t subsystemReset;
        uint32_t adminQueueAttribs;
        uint64_t adminSubmissionQueue;
        uint64_t adminCompletionQueue;
        uint32_t memoryBufferLocation;
        uint32_t memoryBufferSize;
        uint32_t bootPartInfo;
        uint32_t bootPartSelect;
        uint64_t bootPartLocation;
        uint64_t memoryBufferSpaceCtl;
        uint32_t memoryBufferStatus;
        uint32_t memoryBufferElasticitySize;
        uint32_t memoryBufferConstWriteThroughput;
        uint32_t subsystemShutdown;
        uint32_t controllerReadyTimeouts;
    };

    //Not the PRP list from the spec, this holds a prplist we have created and need to free later on.
    struct NvmeQueuePrpl
    {
        uint16_t commandId;
        sl::NativePtr buffer;

        NvmeQueuePrpl(uint16_t cmdId, sl::NativePtr buff) : commandId(cmdId), buffer(buff)
        {}
    };

    struct NvmeQueue
    {
        sl::NativePtr submission;
        sl::NativePtr completion;
        volatile uint32_t* sqDoorbell;
        volatile uint32_t* cqDoorbell;
        size_t entries;
        uint32_t cqHead;
        uint32_t cqPhase;
        uint32_t sqTail;
        uint16_t nextCommandId;
        sl::Vector<NvmeQueuePrpl> prplists;
    };

    union SubmissionQueueEntry
    {
        uint32_t dwords[16];

        struct
        {
            uint8_t opcode;
            uint8_t flags;
            uint16_t commandId;
            uint32_t namespaceId;
            uint32_t cdw2;
            uint32_t cdw3;
            uint64_t metadataPtr;
            uint64_t prp1; //prp1 + prp2 are also collectively called the data pointer
            uint64_t prp2;
            uint32_t cdw10;
            uint32_t cdw11;
            uint32_t cdw12;
            uint32_t cdw13;
            uint32_t cdw14;
            uint32_t cdw15;
        } fields;

        SubmissionQueueEntry();
        volatile SubmissionQueueEntry& operator=(const SubmissionQueueEntry& other) volatile;
    };

    union CompletionQueueEntry
    {
        uint32_t dwords[4];

        struct
        {
            uint32_t result;
            uint32_t reserved;
            uint16_t updatedSqHead;
            uint16_t sqid;
            uint16_t commandId;
            uint16_t status; //bit 0 is the phase bit
        } fields;

        CompletionQueueEntry& operator=(const volatile CompletionQueueEntry& other);
    };

    using NvmeCmdResult = uint32_t;
    
    enum IdentifyCns : uint8_t
    {
        Namespace = 0,
        Controller = 1,
        ActiveNamespacesList = 2,
        NsidDescriptors = 3,
    };

    enum FeatureId : uint8_t
    {
        Arbitration = 1,
        PowerManagement = 2,
        TemperatureThreshold = 4,
        VolatileWriteCache = 6,
        NumberOfQueues = 7,
        InterruptVectorConfig = 9,
    };

    enum FeatureAttrib : uint8_t
    {
        Current = 0b000,
        Default = 0b001,
        Saved = 0b010,
        Supported = 0b011,
    };

    struct NvmeNamespace
    {
        uint32_t nsid;
        size_t blockCount;
        size_t blockSize; 

        NvmeNamespace(uint32_t id) : nsid(id)
        {}
    };
}
