#include <cultures/AustralianCulture.h>
#include <cultures/Helpers.h>

namespace sl::Cultures
{
    bool AustralianCulture::IsAlpha(CharType character)
    {
        if (character >= 'a' && character <= 'z')
            return true;
        if (character >= 'A' && character <= 'Z')
            return true;
        return false;
    }

    bool AustralianCulture::IsUpper(CharType character)
    { return character >= 'A' && character <= 'Z'; }

    bool AustralianCulture::IsLower(CharType character)
    { return character >= 'a' && character <= 'z'; }

    bool AustralianCulture::IsPrintable(CharType character)
    { return character >= ' ' && character <= '~'; }

    bool AustralianCulture::IsDigit(CharType character)
    { return character >= '0' && character <= '9'; }

    bool AustralianCulture::IsHexDigit(CharType character)
    { 
        if (character >= 'a' && character <= 'f')
            return true;
        if (character >= 'A' && character <= 'F')
            return true;
        return IsDigit(character);
    }

    bool AustralianCulture::IsAlphaNum(CharType character)
    { return IsAlpha(character) || IsDigit(character); }

    bool AustralianCulture::IsSpace(CharType character)
    {
        switch (character)
        {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            return true;
            
        default:
            return false;
        }
        __builtin_unreachable();
    }

    CharType AustralianCulture::ToUpper(CharType character)
    {
        if (IsLower(character))
            return character -= 0x20;
        return character;
    }

    CharType AustralianCulture::ToLower(CharType character)
    {
        if (IsUpper(character))
            return character += 0x20;
        return character;
    }

    uint8_t AustralianCulture::GetUInt8(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<uint8_t>(str, start, base); }

    uint16_t AustralianCulture::GetUInt16(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<uint16_t>(str, start, base); }

    uint32_t AustralianCulture::GetUInt32(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<uint32_t>(str, start, base); }

    uint64_t AustralianCulture::GetUInt64(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<uint64_t>(str, start, base); }

    int8_t AustralianCulture::GetInt8(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<int8_t>(str, start, base); }

    int16_t AustralianCulture::GetInt16(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<int16_t>(str, start, base); }

    int32_t AustralianCulture::GetInt32(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<int32_t>(str, start, base); }

    int64_t AustralianCulture::GetInt64(const sl::String& str, size_t start, size_t base)
    { return ConvertStringToInt<int64_t>(str, start, base); }

    sl::String AustralianCulture::ToString(uint8_t num, size_t base)
    { return ConvertIntToString<uint8_t>(num, base, IntStringLength(UINT8_MAX, base)); }

    sl::String AustralianCulture::ToString(uint16_t num, size_t base)
    { return ConvertIntToString<uint16_t>(num, base, IntStringLength(UINT16_MAX, base)); }

    sl::String AustralianCulture::ToString(uint32_t num, size_t base)
    { return ConvertIntToString<uint32_t>(num, base, IntStringLength(UINT32_MAX, base)); }

    sl::String AustralianCulture::ToString(uint64_t num, size_t base)
    { return ConvertIntToString<uint64_t>(num, base, IntStringLength(UINT64_MAX, base)); }
    
    sl::String AustralianCulture::ToString(int8_t num, size_t base)
    { return ConvertIntToString<int8_t>(num, base, IntStringLength(INT8_MIN, base)); }

    sl::String AustralianCulture::ToString(int16_t num, size_t base)
    { return ConvertIntToString<int16_t>(num, base, IntStringLength(INT16_MIN, base)); }

    sl::String AustralianCulture::ToString(int32_t num, size_t base)
    { return ConvertIntToString<int32_t>(num, base, IntStringLength(INT32_MIN, base)); }

    sl::String AustralianCulture::ToString(int64_t num, size_t base)
    { return ConvertIntToString<int64_t>(num, base, IntStringLength(INT64_MIN, base)); }

    size_t AustralianCulture::IntStringLength(int64_t num, size_t base)
    {
        size_t length = 0;
        while (num) 
        {
            length++;
            num /= base;
        };

        return length > 0 ? length : 1;
    }

    size_t AustralianCulture::UIntStringLength(uint64_t num, size_t base)
    {
        size_t length = 0;
        while (num) 
        {
            length++;
            num /= base;
        };

        return length > 0 ? length : 1;
    }
}
