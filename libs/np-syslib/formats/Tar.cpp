#include <formats/Tar.h>
#include <NativePtr.h>

/*
    Useful minimal implementation of ustar format: https://github.com/calccrypto/tar
    Super helpful for debugging some issues I was having (like locating the end of an archive).
*/

namespace sl
{
    size_t ExtractNumber(const uint8_t* str, size_t count)
    {
        size_t out = 0;
        int i = 0;

        while ((i < count) && (str[i] != 0))
            out = (out << 3) | (unsigned int) (str[i++] - '0');
        
        return out;
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

    bool TarHeader::IsZero() const
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < TarSectorSize; i++)
        {
            if (data[0] != 0)
                return false;
        }

        return true;
    }

    const TarHeader* TarHeader::Next() const
    {
        const TarHeader* test = sl::NativePtr((size_t)this).As<const TarHeader>(((SizeInBytes() + 511) / TarSectorSize + 1) * TarSectorSize);
        
        while (test->IsZero())
        {
            if (sl::NativePtr((size_t)test).As<TarHeader>(TarSectorSize)->IsZero())
                return nullptr;
            
            test = test->Next();
        }

        return test;
    }
}
