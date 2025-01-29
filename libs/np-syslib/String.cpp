#include <String.h>
#include <Memory.h>
#include <CppUtils.h>

namespace sl
{
    char emptyString[] = { '\0' };
    
    String::String() : buffer(emptyString), length(0)
    {}

    String::String(const char* const cstr)
    {
        length = MemFind(cstr, 0, NoLimit);

        if (length > 0)
        {
            buffer = new char[length + 1];
            MemCopy(buffer, cstr, length);
            buffer[length] = 0;
        }
        else
            buffer = emptyString;
    }

    String::String(char* const cstr, bool reuseBuffer)
    {
        if (reuseBuffer)
        {
            length = MemFind(cstr, 0, NoLimit);
            buffer = cstr;
        }
        else
        {
            length = MemFind(cstr, 0, NoLimit);
            buffer = new char[length + 1];
            MemCopy(buffer, cstr, length);
            buffer[length] = 0;
        }
    }

    String::String(const char c)
    {
        length = 1;
        buffer = new char[2];
        buffer[0] = c;
        buffer[1] = 0;
    }

    String::String(StringSpan span)
    {
        if (*(span.End() - 1) == 0)
            span = span.Subspan(0, span.Size() - 1);
        
        length = span.Size();
        buffer = new char[span.Size() + 1];
        MemCopy(buffer, span.Begin(), length);
        buffer[length] = 0;
    }

    String::~String()
    {
        if (buffer && buffer != emptyString)
            operator delete[](buffer, length + 1);
        length = 0;
    }

    String::String(const String& other)
    {
        if (other.length > 0)
        {
            length = other.length;
            buffer = new char[length + 1];
            MemCopy(buffer, other.buffer, length);
            buffer[length] = 0;
        }
        else
        {
            length = 0;
            buffer = emptyString;
        }
    }

    String& String::operator=(const String& other)
    {
        if (&other == this)
            return *this;
        
        if (buffer && buffer != emptyString)
            operator delete[](buffer, length + 1);

        length = other.length;
        buffer = new char[length + 1];
        MemCopy(buffer, other.buffer, length);
        buffer[length] = 0;

        return *this;
    }

    String::String(String&& from)
    {
        buffer = nullptr;
        length = 0;
        Swap(buffer, from.buffer);
        Swap(length, from.length);
    }

    String& String::operator=(String&& from)
    {
        if (buffer && buffer != emptyString)
            operator delete[](buffer, length + 1);
        
        buffer = nullptr;
        length = 0;
        Swap(buffer, from.buffer);
        Swap(length, from.length);

        return *this;
    }

    char* String::DetachBuffer()
    {
        char* temp = buffer;
        length = 0;
        buffer = nullptr;
        return temp;
    }

    sl::StringSpan String::Span() const
    {
        return { buffer, length };
    }

    char& String::At(size_t index)
    {
        if (index >= length)
            return buffer[length - 1];
        return buffer[index];
    }

    const char& String::At(size_t index) const
    {
        if (index >= length)
            return buffer[length - 1];
        return buffer[index];
    }

    char& String::operator[](size_t index)
    { return At(index); }

    const char& String::operator[](size_t index) const
    { return At(index); }

    bool String::operator==(const String& other) const
    {
        if (other.length != length)
            return false;
        
        return MemCompare(buffer, other.buffer, length) == 0;
    }

    bool String::operator!=(const String& other) const
    {
        if (other.length != length)
            return true;

        return MemCompare(buffer, other.buffer, length) != 0;
    }

    bool String::operator==(sl::StringSpan span) const
    {
        if (span.Size() != length)
            return false;
        
        return MemCompare(buffer, span.Begin(), length) == 0;
    }

    bool String::operator!=(sl::StringSpan span) const
    {
        if (span.Size() != length)
            return true;
        
        return MemCompare(buffer, span.Begin(), length) != 0;
    }
}
