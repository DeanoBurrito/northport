#pragma once

#include <Span.h>

namespace sl
{
    constexpr size_t TarBlockSize = 512;

    enum class TarEntryType : uint8_t
    {
        Unknown,
        File,
        Directory,
    };

    struct TarHeader
    {
        uint8_t name[100];
        uint8_t mode[8];
        uint8_t uid[8];
        uint8_t gid[8];
        uint8_t size[12];
        uint8_t mtime[12];
        uint8_t checksum[8];
        uint8_t typeflag[1];
        uint8_t linkname[100];
        uint8_t magic[6];
        uint8_t version[2];
        uint8_t uname[32];
        uint8_t gname[32];
        uint8_t devmajor[8];
        uint8_t devminor[8];
        uint8_t prefix[155];
        uint8_t pad[12];

        StringSpan Filename() const;
        StringSpan OwnerName() const;
        StringSpan GroupName() const;
        size_t OwnerId();
        size_t GroupId();
        size_t SizeBytes() const;
        TarEntryType Type() const;
        bool IsZero() const;
        const TarHeader* Next() const;

        [[gnu::always_inline]]
        const void* Data() const
        { return this + 1; }
    };
}
