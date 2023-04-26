#pragma once

#include <stddef.h>
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
        
        ~String();
        String(const String& other);
        String& operator=(const String& other);
        String(String&& from);
        String& operator=(String&& from);

        const char* C_Str() const;
        bool IsEmpty() const;
        size_t Size() const;
        [[nodiscard]]
        char* DetachBuffer();

        String SubString(size_t start, size_t length) const;
        String Concat(const String& other) const;
        String operator+(const String& other) const;
        String& operator+=(const String& other);
        sl::StringSpan Span() const;

        size_t Find(const char token, size_t offset = 0) const;
        size_t FindLast(const char token) const;
        bool BeginsWith(const String& comp) const;
        bool EndsWith(const String& comp) const;
        void TrimStart(size_t amount);
        void TrimEnd(size_t amount);

        char& At(const size_t index);
        const char& At(const size_t index) const;
        char& operator[](size_t index);
        const char& operator[](size_t index) const;

        bool operator==(const String& other) const;
        bool operator!=(const String& other) const;
        bool operator==(sl::StringSpan span) const;
        bool operator!=(sl::StringSpan span) const;
    };

}
