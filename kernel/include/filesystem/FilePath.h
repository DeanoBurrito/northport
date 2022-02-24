#pragma once

#include <String.h>
#include <containers/Vector.h>

namespace Kernel::Filesystem
{
    class FilePath
    {
    private:
        sl::String rawPath;

    public:
        FilePath(const sl::String& path);
        FilePath(const sl::String& path, const sl::String& file);

        const sl::String Filename() const;
        const sl::String ParentDirectory() const;
        const sl::String FullPath() const;
        const sl::Vector<sl::String> Segments() const;
        bool IsAbsolute() const;
    };
}
