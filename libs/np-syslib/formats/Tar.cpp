#include <formats/Tar.h>

namespace sl
{
    //thanks to the osdev-wiki for this code
    size_t ExtractNumber(const uint8_t* str, size_t count)
    {
        size_t n = 0;
        const uint8_t* c = str;

        while (count-- > 0)
        {
            n *= 8;
            n += *c - '0';
            c++;
        }

        return n;
    }
    
    const sl::String TarHeader::Filename() const
    {
        if (fileNamePrefix[0] != 0)
            return sl::String(reinterpret_cast<const char*>(fileNamePrefix)).Concat(reinterpret_cast<const char*>(fileName));
        else
            return sl::String(reinterpret_cast<const char*>(fileName));
    }

    const sl::String TarHeader::LinkedFilename() const
    {
        return sl::String(reinterpret_cast<const char*>(linkName));
    }

    const sl::String TarHeader::OwnerName() const
    {
        return sl::String(reinterpret_cast<const char*>(ownerUserName));
    }

    const sl::String TarHeader::GroupName() const
    {
        return sl::String(reinterpret_cast<const char*>(ownerGroupName));
    }

    uint64_t TarHeader::OwnerId() const
    {
        return ExtractNumber(ownerId, 8);
    }

    uint64_t TarHeader::GroupId() const
    {
        return ExtractNumber(groupId, 8);
    }

    size_t TarHeader::LastModifiedTime() const
    {
        return ExtractNumber(modifiedTime, 12);
    }

    size_t TarHeader::SizeInBytes() const
    {
        return ExtractNumber(fileSize, 12);
    }

    TarEntryType TarHeader::Type() const
    {
        if (entryType == 0 || entryType == '7')
            return TarEntryType::NormalFile;
        return (TarEntryType)entryType;
    }
}
