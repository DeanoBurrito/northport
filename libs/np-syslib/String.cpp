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
        length = memfirst(cstr, 0, 0);

        if (length > 0)
        {
            buffer = new char[length + 1];
            memcopy(cstr, buffer, length);
            buffer[length] = 0;
        }
        else
            buffer = emptyString;
    }

    String::String(char* const cstr, bool reuseBuffer)
    {
        if (reuseBuffer)
        {
            length = memfirst(cstr, 0, 0);
            buffer = cstr;
        }
        else
        {
            length = memfirst(cstr, 0, 0);
            buffer = new char[length + 1];
            memcopy(cstr, buffer, length);
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
        sl::memcopy(span.Begin(), buffer, length);
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
            memcopy(other.buffer, buffer, length);
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
        memcopy(other.buffer, buffer, length);
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

    const char* String::C_Str() const
    { return buffer; }

    bool String::IsEmpty() const
    { return length == 0; }

    size_t String::Size() const
    { return length; }

    char* String::DetachBuffer()
    {
        char* temp = buffer;
        length = 0;
        buffer = nullptr;
        return temp;
    }

    String String::SubString(size_t start, size_t len) const
    {
        if (start + len > length || len == (size_t)-1)
            len = length - start;

        char* tempBuffer = new char[len + 1];
        sl::memcopy(buffer, start, tempBuffer, 0, len);
        tempBuffer[len] = 0;
        
        return String(tempBuffer, true);
    }

    String String::Concat(const String& other) const
    {
        if (length == 0)
            return other;
        if (other.length == 0)
            return *this;
        if (length == 0 && other.length == 0)
            return "";

        const size_t newStrLength = length + other.length;
        char* buff = new char[newStrLength + 1];
        sl::memcopy(buffer, buff, length);
        sl::memcopy(other.buffer, 0, buff, length, other.length);
        buff[newStrLength] = 0;

        return String(buff, true);
    }

    String String::operator+(const String& other) const
    {
        return Concat(other);
    }

    String& String::operator+=(const String& other)
    {
        sl::String s = Concat(other);
        sl::Swap(*this, s);
        return *this;
    }

    sl::StringSpan String::Span() const
    {
        return { buffer, length };
    }

    size_t String::Find(const char token, size_t offset) const
    {
        return memfirst(buffer, offset, token, length);
    }

    size_t String::FindLast(const char token) const
    {
        size_t lastFound = 0;
        size_t nextFound = 0;
        while (nextFound != length)
        {
            lastFound = nextFound;
            nextFound = memfirst(buffer, lastFound + 1, token, length);
        }
        return lastFound;
    }

    bool String::BeginsWith(const String& comp) const
    {
        if (comp.Size() > Size())
            return false;

        for (size_t i = 0; i < comp.Size(); i++)
        {
            if (At(i) != comp.At(i))
                return false;
        }
        return true;
    }

    bool String::EndsWith(const String& comp) const
    {
        if (comp.Size() > Size())
            return false;

        for (size_t i = Size() - comp.Size(); i < Size(); i++)
        {
            if (At(i) != comp.At(i))
                return false;
        }
        return true;
    }

    void String::TrimStart(size_t amount)
    {
        if (length < amount)
            length = 0;
        else
            buffer += amount;
    }

    void String::TrimEnd(size_t amount)
    { 
        if (length < amount)
            length = 0;
        else
            length -= amount;
        buffer[length] = 0;
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
        
        return sl::memcmp(buffer, other.buffer, length) == 0;
    }

    bool String::operator!=(const String& other) const
    {
        if (other.length != length)
            return true;

        return sl::memcmp(buffer, other.buffer, length) != 0;
    }

    bool String::operator==(sl::StringSpan span) const
    {
        if (span.Size() != length)
            return false;
        
        return sl::memcmp(buffer, span.Begin(), length) == 0;
    }

    bool String::operator!=(sl::StringSpan span) const
    {
        if (span.Size() != length)
            return true;
        
        return sl::memcmp(buffer, span.Begin(), length) != 0;
    }
}
