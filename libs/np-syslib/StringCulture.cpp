#include <StringCulture.h>

namespace sl
{
    StrayaCulture defaultCulture;
    StringCulture* StringCulture::current = &defaultCulture;

    bool StrayaCulture::IsAlpha(int character)
    {
        if (character >= 'a' && character <= 'z')
            return true;
        if (character >= 'A' && character <= 'Z')
            return true;
        return false;
    }

    bool StrayaCulture::IsUpper(int character)
    { return character >= 'A' && character <= 'Z'; }

    bool StrayaCulture::IsLower(int character)
    { return character >= 'a' && character <= 'z'; }

    bool StrayaCulture::IsPrintable(int character)
    { return character >= ' ' && character <= '~'; }

    bool StrayaCulture::IsDigit(int character)
    { return character >= '0' && character <= '9'; }

    bool StrayaCulture::IsHexDigit(int character)
    { 
        if (character >= 'a' && character <= 'f')
            return true;
        if (character >= 'A' && character <= 'F')
            return true;
        return IsDigit(character);
    }

    bool StrayaCulture::IsAlphaNum(int character)
    {
        return IsAlpha(character) || IsDigit(character);
    }

    bool StrayaCulture::IsSpace(int character)
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
    }
    
    int StrayaCulture::ToUpper(int character)
    {
        if (IsLower(character))
            return character -= 0x20;
        return character;
    }

    int StrayaCulture::ToLower(int character)
    {
        if (IsUpper(character))
            return character += 0x20;
        return character;
    }

    using namespace sl::Helpers;

    bool StrayaCulture::TryGetUInt8(uint8_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetUInt<uint8_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetUInt16(uint16_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetUInt<uint16_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetUInt32(uint32_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetUInt<uint32_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetUInt64(uint64_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetUInt<uint64_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetInt8(int8_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetInt<int8_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetInt16(int16_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetInt<int16_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetInt32(int32_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetInt<int32_t>(out, str, start, base); 
    }

    bool StrayaCulture::TryGetInt64(int64_t* out, const string& str, size_t start, size_t base)
    { 
        return TryGetInt<int64_t>(out, str, start, base); 
    }

    string StrayaCulture::ToString(uint8_t num, size_t base)
    { 
        return String(UIntToString<uint8_t, 4>(num, base), true); 
    }

    string StrayaCulture::ToString(uint16_t num, size_t base)
    { 
        return String(UIntToString<uint16_t, 7>(num, base), true); 
    }

    string StrayaCulture::ToString(uint32_t num, size_t base)
    { 
        return String(UIntToString<uint32_t, 12>(num, base), true); 
    }

    string StrayaCulture::ToString(uint64_t num, size_t base)
    { 
        return String(UIntToString<uint64_t, 21>(num, base), true); 
    }

    string StrayaCulture::ToString(int8_t num, size_t base)
    { 
        return String(IntToString<int8_t, 4>(num, base), true); 
    }

    string StrayaCulture::ToString(int16_t num, size_t base)
    { 
        return String(IntToString<int16_t, 7>(num, base), true); 
    }

    string StrayaCulture::ToString(int32_t num, size_t base)
    { 
        return String(IntToString<int32_t, 12>(num, base), true); 
    }

    string StrayaCulture::ToString(int64_t num, size_t base)
    { 
        return String(IntToString<int64_t, 21>(num, base), true); 
    }
}
