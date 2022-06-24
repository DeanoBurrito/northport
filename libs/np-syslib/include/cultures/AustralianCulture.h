#pragma once

#include <StringCulture.h>

namespace sl::Cultures
{
    class AustralianCulture : public StringCulture
    {
    public:
        bool IsAlpha(CharType character) override;
        bool IsUpper(CharType character) override;
        bool IsLower(CharType character) override;
        bool IsPrintable(CharType character) override;
        bool IsDigit(CharType character) override;
        bool IsHexDigit(CharType character) override;
        bool IsAlphaNum(CharType character) override;
        bool IsSpace(CharType character) override;

        CharType ToUpper(CharType character) override;
        CharType ToLower(CharType character) override;

        uint8_t GetUInt8(const sl::String& str, size_t start, size_t base) override;
        uint16_t GetUInt16(const sl::String& str, size_t start, size_t base) override;
        uint32_t GetUInt32(const sl::String& str, size_t start, size_t base) override;
        uint64_t GetUInt64(const sl::String& str, size_t start, size_t base) override;
        
        int8_t GetInt8(const sl::String& str, size_t start, size_t base) override;
        int16_t GetInt16(const sl::String& str, size_t start, size_t base) override;
        int32_t GetInt32(const sl::String& str, size_t start, size_t base) override;
        int64_t GetInt64(const sl::String& str, size_t start, size_t base) override;

        sl::String ToString(uint8_t num, size_t base) override;
        sl::String ToString(uint16_t num, size_t base) override;
        sl::String ToString(uint32_t num, size_t base) override;
        sl::String ToString(uint64_t num, size_t base) override;

        sl::String ToString(int8_t num, size_t base) override;
        sl::String ToString(int16_t num, size_t base) override;
        sl::String ToString(int32_t num, size_t base) override;
        sl::String ToString(int64_t num, size_t base) override;

        size_t IntStringLength(int64_t num, size_t base = Base::Decimal) override;
        size_t UIntStringLength(uint64_t num, size_t base = Base::Decimal) override;
    };
}
