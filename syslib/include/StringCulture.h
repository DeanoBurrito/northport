#pragma once

#include <stdint.h>

namespace sl
{
    class StringCulture
    {
    private:
    public:
        static StringCulture* current;

        virtual bool IsAlpha(char character) = 0;
        virtual bool IsUpper(char character) = 0;
        virtual bool IsLower(char character) = 0;
        virtual bool IsPrintable(char character) = 0;
        virtual bool IsDigit(char character) = 0;
        virtual bool IsHexDigit(char character) = 0;
        virtual bool IsAlphaNum(char character) = 0;
        virtual bool IsSpace(char character) = 0;

        virtual char ToUpper(char character) = 0;
        virtual char ToLower(char character) = 0;
    };

    class StrayaCulture : public StringCulture
    {
        bool IsAlpha(char character);
        bool IsUpper(char character);
        bool IsLower(char character);
        bool IsPrintable(char character);
        bool IsDigit(char character);
        bool IsHexDigit(char character);
        bool IsAlphaNum(char character);
        bool IsSpace(char character);

        char ToUpper(char character);
        char ToLower(char character);
    };
}
