#pragma once

#include <stdint.h>
#include <String.h>

namespace sl
{
    constexpr size_t TarSectorSize = 512;
    
    enum class TarEntryType : uint8_t
    {
        NormalFile = '0',
        HardLink = '1',
        SymLink = '2',
        CharDevice = '3',
        BlockDevice = '4',
        Directory = '5',
        FIFOPipe = '6',
    };
    
    struct TarHeader
    {
    public:
        uint8_t fileName[100];
        uint8_t fileMode[8];
        uint8_t ownerId[8];
        uint8_t groupId[8];
        uint8_t fileSize[12]; //NOTE: this is octal, not decimal 
        uint8_t modifiedTime[12]; //also octal
        uint8_t checksum[8];
        uint8_t entryType;
        uint8_t linkName[100];
        uint8_t archiveSignature[6]; //should be "USTAR\0"
        uint8_t archiveVersion[2]; //"00"
        uint8_t ownerUserName[32];
        uint8_t ownerGroupName[32];
        uint8_t deviceMajorNumber[8];
        uint8_t deviceMinorNumber[8];
        uint8_t fileNamePrefix[155];

        TarHeader() = default;

        [[gnu::always_inline]] inline
        static TarHeader* FromExisting(void* data)
        { return reinterpret_cast<TarHeader*>(data); }

        const sl::String Filename() const;
        const sl::String LinkedFilename() const;
        const sl::String OwnerName() const;
        const sl::String GroupName() const;
        uint64_t OwnerId() const;
        uint64_t GroupId() const;
        size_t LastModifiedTime() const;
        size_t SizeInBytes() const;
        TarEntryType Type() const;
        bool IsZero() const;

        //returns the next tar header in an archieve, nullptr if EOF.
        const TarHeader* Next() const;
    };
}
