#include <StringCulture.h>
#include <cultures/AustralianCulture.h>

namespace sl
{
    Cultures::AustralianCulture strayaTime; //yeah baby, straya time. 8D
    StringCulture* currentCulture = &strayaTime;

    bool IsAlpha(CharType character)
    { return currentCulture->IsAlpha(character); }

    bool IsUpper(CharType character)
    { return currentCulture->IsUpper(character); }

    bool IsLower(CharType character)
    { return currentCulture->IsLower(character); }

    bool IsPrintable(CharType character)
    { return currentCulture->IsPrintable(character); }

    bool IsDigit(CharType character)
    { return currentCulture->IsDigit(character); }

    bool IsHexDigit(CharType character)
    { return currentCulture->IsHexDigit(character); }

    bool IsAlphaNum(CharType character)
    { return currentCulture->IsAlphaNum(character); }

    bool IsSpace(CharType character)
    { return currentCulture->IsSpace(character); }

    CharType ToUpper(CharType character)
    { return currentCulture->ToUpper(character); }

    CharType ToLower(CharType character)
    { return currentCulture->ToLower(character); }

    uint8_t GetUInt8(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetUInt8(str, start, base); }

    uint16_t GetUInt16(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetUInt16(str, start, base); }

    uint32_t GetUInt32(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetUInt32(str, start, base); }

    uint64_t GetUInt64(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetUInt64(str, start, base); }
    
    int8_t GetInt8(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetInt8(str, start, base); }

    int16_t GetInt16(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetInt16(str, start, base); }

    int32_t GetInt32(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetInt32(str, start, base); }

    int64_t GetInt64(const sl::String& str, size_t start, size_t base)
    { return currentCulture->GetInt64(str, start, base); }

    sl::String ToString(uint8_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(uint16_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(uint32_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(uint64_t num, size_t base)
    { return currentCulture->ToString(num, base); }
    
    sl::String ToString(int8_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(int16_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(int32_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    sl::String ToString(int64_t num, size_t base)
    { return currentCulture->ToString(num, base); }

    size_t IntStringLength(int64_t num, size_t base)
    { return currentCulture->IntStringLength(num, base); }

    size_t UIntStringLength(uint64_t num, size_t base)
    { return currentCulture->UIntStringLength(num, base); }

    StringCulture* StringCulture::GetCurrent()
    { return currentCulture; }

    void StringCulture::SetCurrent(StringCulture* culture)
    { currentCulture = culture; }
}
