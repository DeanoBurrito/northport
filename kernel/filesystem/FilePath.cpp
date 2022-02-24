#include <filesystem/FilePath.h>
#include <Utilities.h>

namespace Kernel::Filesystem
{
    FilePath::FilePath(const sl::String& path)
    {
        rawPath = path;
        if (rawPath.EndsWith('/'))
            rawPath.TrimEnd(1);
    }

    FilePath::FilePath(const sl::String& path, const sl::String& file)
    {
        if (!path.EndsWith('/'))
            rawPath = path.Concat('/').Concat(file);
        else
            rawPath = path.Concat(file);
        if (rawPath.EndsWith('/'))
            rawPath.TrimEnd(1);
    }

    const sl::String FilePath::Filename() const
    {
        size_t lastSeparator = rawPath.FindLast('/');
        if (lastSeparator == 0)
            return rawPath;
        return rawPath.SubString(lastSeparator, string::NoPos);
    }

    const sl::String FilePath::ParentDirectory() const
    {
        size_t lastSeparator = rawPath.FindLast('/');
        if (lastSeparator == 0)
            return "";
        return rawPath.SubString(0, lastSeparator);
    }

    const sl::String FilePath::FullPath() const
    {
        return rawPath;
    }

    const sl::Vector<sl::String> FilePath::Segments() const
    {
        sl::Vector<sl::String> segments;
        size_t segmentBegin = 0;
        for (size_t i = 0; i < rawPath.Size(); i++)
        {
            if (rawPath[i] != '/')
                continue;
            
            if (segmentBegin - i == 0)
            {
                segmentBegin = i + 1;
                continue; //empty segment, ignore it
            }
            
            segments.PushBack(rawPath.SubString(segmentBegin, i - segmentBegin));

            segmentBegin = i + 1;
            i++;
            continue;
        }
        segments.PushBack(rawPath.SubString(segmentBegin, string::NoPos));

        return segments;
    }

    bool FilePath::IsAbsolute() const
    { return rawPath.BeginsWith('/'); }
}
