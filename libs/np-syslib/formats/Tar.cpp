#include <formats/Tar.h>
#include <Memory.h>
#include <Maths.h>

namespace sl
{
    size_t ReadNumber(const uint8_t* src, size_t maxLength)
    {
        size_t out = 0;
        for (size_t i = 0; (i < maxLength) && (src[i] != 0); i++)
            out = (out << 3) | (size_t)(src[i] - '0');

        return out;
    }
    
    StringSpan TarHeader::Filename() const
    {
        //TODO: we should honour the prefix field as well
        const size_t length = sl::Min<size_t>(sl::MemFind(name, 0, 100), 100);
        return StringSpan(reinterpret_cast<const char*>(name), length); //maximum safety, can't do that in rust B)
    }

    StringSpan TarHeader::OwnerName() const
    {
        const size_t length = sl::Min<size_t>(sl::MemFind(uname, 0, 32), 32);
        return StringSpan(reinterpret_cast<const char*>(uname), length);
    }

    StringSpan TarHeader::GroupName() const
    {
        const size_t length = sl::Min<size_t>(sl::MemFind(gname, 0, 32), 32);
        return StringSpan(reinterpret_cast<const char*>(gname), length);
    }

    size_t TarHeader::OwnerId()
    { return ReadNumber(uid, 8); }

    size_t TarHeader::GroupId()
    { return ReadNumber(gid, 8); }

    size_t TarHeader::SizeBytes() const
    { return ReadNumber(size, 12); }

    TarEntryType TarHeader::Type() const
    {
        switch (typeflag[0])
        {
        case 0:
        case '0':
            return TarEntryType::File;
        case '5':
            return TarEntryType::Directory;
        default:
            return TarEntryType::Unknown;
        }
    }

    bool TarHeader::IsZero() const
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < TarBlockSize; i++)
        {
            if (data[i] != 0)
                return false;
        }

        return true;
    }

    const TarHeader* TarHeader::Next() const
    {
        const size_t blocks = sl::AlignUp(SizeBytes(), TarBlockSize) / TarBlockSize;
        return this + blocks + 1;
    }
}
