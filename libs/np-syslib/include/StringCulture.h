#pragma once

#include <stddef.h>
#include <stdint.h>
#include <String.h>

namespace sl
{
    namespace Base
    {
        constexpr static size_t Binary = 2;
        constexpr static size_t Octal = 8;
        constexpr static size_t Decimal = 10;
        constexpr static size_t Hex = 16;
    };

    /*
        These functions just wrap calling the function of the same name, 
        on the current culture. Useful shorthands, and mostly compatible with 
        their C standard library cousins.
    */
    using CharType = int;

    bool IsAlpha(CharType character);
    bool IsUpper(CharType character);
    bool IsLower(CharType character);
    bool IsPrintable(CharType character);
    bool IsDigit(CharType character);
    bool IsHexDigit(CharType character);
    bool IsAlphaNum(CharType character);
    bool IsSpace(CharType character);

    CharType ToUpper(CharType character);
    CharType ToLower(CharType character);

    uint8_t GetUInt8(const sl::String& str, size_t start, size_t base);
    uint16_t GetUInt16(const sl::String& str, size_t start, size_t base);
    uint32_t GetUInt32(const sl::String& str, size_t start, size_t base);
    uint64_t GetUInt64(const sl::String& str, size_t start, size_t base);
    
    int8_t GetInt8(const sl::String& str, size_t start, size_t base);
    int16_t GetInt16(const sl::String& str, size_t start, size_t base);
    int32_t GetInt32(const sl::String& str, size_t start, size_t base);
    int64_t GetInt64(const sl::String& str, size_t start, size_t base);

    sl::String ToString(uint8_t num, size_t base);
    sl::String ToString(uint16_t num, size_t base);
    sl::String ToString(uint32_t num, size_t base);
    sl::String ToString(uint64_t num, size_t base);

    sl::String ToString(int8_t num, size_t base);
    sl::String ToString(int16_t num, size_t base);
    sl::String ToString(int32_t num, size_t base);
    sl::String ToString(int64_t num, size_t base);

    size_t IntStringLength(int64_t num, size_t base = Base::Decimal);
    size_t UIntStringLength(uint64_t num, size_t base = Base::Decimal);

    /*
        Most of the static functions of StringCulture defer to the function of the same name
        under the current culture. To add a new culture, simply override the class,
        and implement all the pure virtual functions. There are some helper functions in `StringHelpers.h`

        Characters are represented using 'int' here, as UTF-8 support is planned oneday, even though the string class currently uses chars.
    */
    class StringCulture
    {
    public:
        static StringCulture* GetCurrent();
        static void SetCurrent(StringCulture* culture);

        virtual bool IsAlpha(CharType character) = 0;
        virtual bool IsUpper(CharType character) = 0;
        virtual bool IsLower(CharType character) = 0;
        virtual bool IsPrintable(CharType character) = 0;
        virtual bool IsDigit(CharType character) = 0;
        virtual bool IsHexDigit(CharType character) = 0;
        virtual bool IsAlphaNum(CharType character) = 0;
        virtual bool IsSpace(CharType character) = 0;

        virtual CharType ToUpper(CharType character) = 0;
        virtual CharType ToLower(CharType character) = 0;

        virtual uint8_t GetUInt8(const sl::String& str, size_t start, size_t base) = 0;
        virtual uint16_t GetUInt16(const sl::String& str, size_t start, size_t base) = 0;
        virtual uint32_t GetUInt32(const sl::String& str, size_t start, size_t base) = 0;
        virtual uint64_t GetUInt64(const sl::String& str, size_t start, size_t base) = 0;
        
        virtual int8_t GetInt8(const sl::String& str, size_t start, size_t base) = 0;
        virtual int16_t GetInt16(const sl::String& str, size_t start, size_t base) = 0;
        virtual int32_t GetInt32(const sl::String& str, size_t start, size_t base) = 0;
        virtual int64_t GetInt64(const sl::String& str, size_t start, size_t base) = 0;

        virtual sl::String ToString(uint8_t num, size_t base) = 0;
        virtual sl::String ToString(uint16_t num, size_t base) = 0;
        virtual sl::String ToString(uint32_t num, size_t base) = 0;
        virtual sl::String ToString(uint64_t num, size_t base) = 0;

        virtual sl::String ToString(int8_t num, size_t base) = 0;
        virtual sl::String ToString(int16_t num, size_t base) = 0;
        virtual sl::String ToString(int32_t num, size_t base) = 0;
        virtual sl::String ToString(int64_t num, size_t base) = 0;

        virtual size_t IntStringLength(int64_t num, size_t base = Base::Decimal) = 0;
        virtual size_t UIntStringLength(uint64_t num, size_t base = Base::Decimal) = 0;
    };
}
