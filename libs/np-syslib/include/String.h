#pragma once

#include <Span.h>

namespace sl
{
    class String
    {
    private:
        char* buffer;
        size_t length;

    public:
        constexpr static size_t NoPos = (size_t)-1;

        String();
        String(const char* const cstr);
        String(char* const cstr, bool reuseBuffer);
        String(const char c);
        String(StringSpan span);
        
        ~String();
        String(const String& other);
        String& operator=(const String& other);
        String(String&& from);
        String& operator=(String&& from);

        inline bool IsEmpty() const
        { 
            return length == 0; 
        }

        inline size_t Size() const
        {
            return length;
        }

        inline const char* C_Str() const
        {
            return buffer;
        }

        [[nodiscard]]
        char* DetachBuffer();

        sl::StringSpan Span() const;

        char& At(const size_t index);
        const char& At(const size_t index) const;
        char& operator[](size_t index);
        const char& operator[](size_t index) const;

        bool operator==(const String& other) const;
        bool operator!=(const String& other) const;
        bool operator==(sl::StringSpan span) const;
        bool operator!=(sl::StringSpan span) const;
        //TOOD: add StringSpan versions of comparison functions (or just replace them, and add a String::Span() operator)
    };
}
