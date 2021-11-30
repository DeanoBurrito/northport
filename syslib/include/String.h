#pragma once

#include <stddef.h>

namespace sl
{
    class String
    {
    private:
        char* buffer;
        size_t length;
        String(char* const cstr, bool reuseBuffer);

    public:
        constexpr static size_t NoPos = (size_t)-1;

        String();
        String(const char* const cstr);
        String(const char c);
        
        ~String();
        String(const String& other);
        String& operator=(const String& other);
        String(String&& from);
        String& operator=(String&& from);

        const char* C_Str() const;
        bool IsEmpty() const;
        size_t Size() const;
        String SubString(size_t start, size_t length) const;

        size_t Find(const char token, size_t offset = 0);

        char& At(const size_t index);
        const char& At(const size_t index) const;
        char& operator[](size_t index);
        const char& operator[](size_t index) const;
    };

}

using string = sl::String;
