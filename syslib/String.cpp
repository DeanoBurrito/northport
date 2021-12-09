#include <String.h>
#include <Memory.h>

namespace sl
{
    char emptyString[] = { '\0' };
    
    String::String() : buffer(emptyString), length(0)
    {}

    String::String(const char* const cstr)
    {
        length = memfirst(cstr, 0, 0);
        buffer = new char[length + 1];
        memcopy(cstr, buffer, length);
        buffer[length] = 0;
    }

    String::String(char* const cstr, bool reuseBuffer)
    {
        length = memfirst(cstr, 0, 0);
        buffer = cstr;
    }

    String::String(const char c)
    {
        length = 1;
        buffer = new char[2];
        buffer[0] = c;
        buffer[1] = 0;
    }

    String::~String()
    {
        if (buffer)
            delete[] buffer;
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
    }

    String& String::operator=(const String& other)
    {
        if (buffer)
            delete[] buffer;

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
        swap(buffer, from.buffer);
        swap(length, from.length);
    }

    String& String::operator=(String&& from)
    {
        if (buffer)
            delete[] buffer;
        
        buffer = nullptr;
        length = 0;
        swap(buffer, from.buffer);
        swap(length, from.length);

        return *this;
    }

    const char* String::C_Str() const
    { return buffer; }

    bool String::IsEmpty() const
    { return length > 0; }

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
        if (start + len > length)
            len = length - start;

        char* tempBuffer = new char[len + 1];
        sl::memcopy(buffer, start, tempBuffer, 0, len);
        tempBuffer[len] = 0;
        
        //using private-ctor which re-uses the existing buffer, rather than a copy
        return move(String(tempBuffer, true));
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
        string s = Concat(other);
        sl::swap(*this, s);
        return *this;
    }

    size_t String::Find(const char token, size_t offset)
    {
        return memfirst(buffer, offset, token, length);
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
}
