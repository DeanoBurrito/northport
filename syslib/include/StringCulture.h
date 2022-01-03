#pragma once

#include <stdint.h>
#include <stddef.h>
#include <String.h>

namespace sl
{
    namespace Base
    {
        constexpr inline size_t BINARY = 2;
        constexpr inline size_t OCTAL = 8;
        constexpr inline size_t DECIMAL = 10;
        constexpr inline size_t HEX = 16;
    }
    
    /*
        Yes, I know haha. I apologize to anyone who wants to add extra languages. 
        
        The design here is that you override StringCulture, and install it as the default culture (or in some global location, tbd).
        Then you can override all the functions below, according to your local language, and what makes sense.
        The TryGet[U]Int() and ToString functions have templated implementations up above that you're welcome to use, 
        if your language uses arabic numerals. Otherwise I'm afraid you're on your own.

        The reference implementation is in 'StrayaCulture' (Australian, for those not in the know haha). A little joke I can curse myself for later.

        Characters are represented using 'int' here, as UTF-8 support is planned oneday, even though the string class currently uses chars.
    */
    class StringCulture
    {
    public:
        static StringCulture* current;

        virtual bool IsAlpha(int character) = 0;
        virtual bool IsUpper(int character) = 0;
        virtual bool IsLower(int character) = 0;
        virtual bool IsPrintable(int character) = 0;
        virtual bool IsDigit(int character) = 0;
        virtual bool IsHexDigit(int character) = 0;
        virtual bool IsAlphaNum(int character) = 0;
        virtual bool IsSpace(int character) = 0;

        virtual int ToUpper(int character) = 0;
        virtual int ToLower(int character) = 0;

        virtual bool TryGetUInt8(uint8_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetUInt16(uint16_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetUInt32(uint32_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetUInt64(uint64_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetInt8(int8_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetInt16(int16_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetInt32(int32_t* out, const string& str, size_t start, bool isHex = false) = 0;
        virtual bool TryGetInt64(int64_t* out, const string& str, size_t start, bool isHex = false) = 0;

        virtual string ToString(uint8_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(uint16_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(uint32_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(uint64_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(int8_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(int16_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(int32_t num, size_t base = Base::DECIMAL) = 0;
        virtual string ToString(int64_t num, size_t base = Base::DECIMAL) = 0;
    };

    //a collection of useful string/number conversions that work in most languages. Of course I want the option to override them,
    //but these stop duplicate of most of the code between cultures that share the arabic number system.
    namespace Helpers
    {
        template<typename IntType> [[gnu::always_inline]]
        inline bool TryGetInt(IntType* out, const string& str, size_t start, bool hex)
        {
            *out = 0;

            bool negative = false;
            if (str[start] == '-')
                negative = true;

            if (hex)
            {
                for (size_t i = start + (negative ? 1 : 0); i < str.Size(); i++)
                {
                    if (!StringCulture::current->IsHexDigit(str[i]))
                        break;
                    
                    *out *= 0x10;
                    
                    char c = str[i];
                    if (c > '9')
                        *out += c - 'A' + 10;
                    else
                        *out += c - '0';
                }
            }
            else
            {
                for (size_t i = start + (negative ? 1 : 0); i < str.Size(); i++)
                {
                    if (!StringCulture::current->IsDigit(str[i]))
                        break;
                    
                    *out *= 10;
                    
                    char c = str[i];
                    *out += c - '0';
                }
            }

            if (negative)
                *out = -(*out);

            return true;
        }

        template<typename UIntType> [[gnu::always_inline]]
        inline bool TryGetUInt(UIntType* out, const string& str, size_t start, bool hex)
        {
            *out = 0;

            if (hex)
            {
                for (size_t i = start; i < str.Size(); i++)
                {
                    if (!StringCulture::current->IsHexDigit(str[i]))
                        break;
                    
                    *out *= 0x10;
                    
                    char c = str[i];
                    if (c > '9')
                        *out += c - 'A' + 10;
                    else
                        *out += c - '0';
                }
            }
            else
            {
                for (size_t i = start; i < str.Size(); i++)
                {
                    if (!StringCulture::current->IsDigit(str[i]))
                        break;
                    
                    *out *= 10;
                    
                    char c = str[i];
                    *out += c - '0';
                }
            }

            return true;
        }

        template<typename UIntType, size_t bufferLength> [[gnu::always_inline]]
        inline char* UIntToString(UIntType num, size_t base)
        {
            if (base < 2 || base > 32)
                return 0;
            
            char* buffer = new char[bufferLength];
            size_t index = 0;

            while (num)
            {
                UIntType remainder = num % base;
                num = num / base;
                if (remainder < 10)
                    buffer[index] = '0' + remainder;
                else
                    buffer[index] = 'A' + remainder - 10;
                index++;
            }

            if (index == 0 && num == 0)
            {
                buffer[index] = '0';
                index++;
            }

            buffer[index] = 0;
            char temp;
            for (size_t i = 0; i < index / 2; i++)
            {
                temp = buffer[i];
                buffer[i] = buffer[index - i - 1];
                buffer[index - i - 1] = temp;
            }

            return buffer;
        }

        template<typename IntType, size_t bufferLength> [[gnu::always_inline]]
        inline char* IntToString(IntType num, size_t base)
        {
            if (base < 2 || base > 32)
                return 0;
            
            char* buffer = new char[bufferLength];
            size_t index = 0;
            bool appendMinus = 0;

            if (num < 0)
            {
                appendMinus = true;
                num = -num;
            }

            while (num)
            {
                IntType remainder = num % base;
                num = num / base;

                if (remainder < 10)
                    buffer[index] = '0' + remainder;
                else
                    buffer[index] = 'A' + remainder - 10;
                index++;
            }

            if (index == 0 && num == 0)
            {
                buffer[index] = '0';
                index++;
            }

            if (appendMinus)
            {
                buffer[index] = '-';
                index++;
            }

            buffer[index] = 0;
            char temp;
            for (size_t i = 0; i < index / 2; i++)
            {
                temp = buffer[i];
                buffer[i] = buffer[index - i - 1];
                buffer[index - i - 1] = temp;
            }

            return buffer;
        }
    }

    class StrayaCulture : public StringCulture
    {   
    public:
        bool IsAlpha(int character);
        bool IsUpper(int character);
        bool IsLower(int character);
        bool IsPrintable(int character);
        bool IsDigit(int character);
        bool IsHexDigit(int character);
        bool IsAlphaNum(int character);
        bool IsSpace(int character);

        int ToUpper(int character);
        int ToLower(int character);

        bool TryGetUInt8(uint8_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetUInt16(uint16_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetUInt32(uint32_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetUInt64(uint64_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetInt8(int8_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetInt16(int16_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetInt32(int32_t* out, const string& str, size_t start, bool isHex = false);
        bool TryGetInt64(int64_t* out, const string& str, size_t start, bool isHex = false);

        string ToString(uint8_t num, size_t base = Base::DECIMAL);
        string ToString(uint16_t num, size_t base = Base::DECIMAL);
        string ToString(uint32_t num, size_t base = Base::DECIMAL);
        string ToString(uint64_t num, size_t base = Base::DECIMAL);
        string ToString(int8_t num, size_t base = Base::DECIMAL);
        string ToString(int16_t num, size_t base = Base::DECIMAL);
        string ToString(int32_t num, size_t base = Base::DECIMAL);
        string ToString(int64_t num, size_t base = Base::DECIMAL);
    };
}
