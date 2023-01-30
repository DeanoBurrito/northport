#pragma once

#include <stdint.h>

namespace Npk::Drivers
{
    //located at 0 in the config space
    struct [[gnu::packed]] ControllerProps
    {
        uint64_t capabilities;
        uint32_t version;
        uint32_t interruptMaskSet;
        uint32_t interruptMaskClear;
        uint32_t config;
        uint32_t reserved;
        uint32_t status;
        uint32_t nvmeSubsystemReset;
        uint32_t aqa;
        uint64_t asq;
        uint64_t acq;
        uint32_t cmbLocation;
        uint32_t cmbSize;
        uint32_t bootPartInfo;
        uint32_t bootPartReadSelect;
        uint64_t bootPartLocation;
        uint64_t cmbSpaceControl;
        uint32_t cmbStatus;
        uint32_t cmbElasticitySize;
        uint32_t cmbWriteThroughput;
        uint32_t nvmShutdown;
        uint32_t readyTimeouts;
    };

    union SqEntry
    {
        uint32_t dw[16];

        struct [[gnu::packed]]
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
    };

    union CqEntry
    {
        uint32_t dw[4];

        struct [[gnu::packed]]
        {
            uint32_t result;
            uint32_t reserved;
            uint16_t updatedSqHead;
            uint16_t sqid;
            uint16_t commandId;
            uint16_t status; //bit 0 is the phase bit
        } fields;
    };
}
