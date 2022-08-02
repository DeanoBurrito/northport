#pragma once

#include <devices/interfaces/GenericBlock.h>

namespace Kernel::Filesystem
{
    constexpr uint64_t GptSignature = 0x5452415020494645;
    
    //found at LBA1 and LBA-1
    struct [[gnu::packed]] GptHeader
    {
        uint64_t signature; //must contain ASCII "EFI PART"
        uint32_t revision;
        uint32_t headerSize;
        uint32_t headerCrc;
        uint32_t reserved;
        uint64_t myLba;
        uint64_t alternateLba;
        uint64_t firstUsableLba;
        uint64_t lastUsableLba;
        uint8_t diskGuid[16];
        uint64_t partEntryLba;
        uint32_t partEntryCount;
        uint32_t partEntrySize;
        uint32_t partEntryArrayCrc;
    };

    struct [[gnu::packed]] GptEntry
    {
        uint8_t partTypeGuid[16]; //0 = unused
        uint8_t partGuid[16];
        uint64_t startLba;
        uint64_t endLba;
        uint64_t attributes;
        uint16_t name[36]; //encoded as UTF16
    };
}
