#include <StringCulture.h>

namespace sl
{
    StrayaCulture defaultCulture;
    StringCulture* StringCulture::current = &defaultCulture;

    bool StrayaCulture::IsAlpha(char character)
    {
        if (character >= 'a' && character <= 'z')
            return true;
        if (character >= 'A' && character <= 'Z')
            return true;
        return false;
    }

    bool StrayaCulture::IsUpper(char character)
    { return character >= 'A' && character <= 'Z'; }

    bool StrayaCulture::IsLower(char character)
    { return character >= 'a' && character <= 'z'; }

    bool StrayaCulture::IsPrintable(char character)
    { return character >= ' ' && character <= '~'; }

    bool StrayaCulture::IsDigit(char character)
    { return character >= '0' && character <= '9'; }

    bool StrayaCulture::IsHexDigit(char character)
    { 
        if (character >= 'a' && character <= 'f')
            return true;
        if (character >= 'A' && character <= 'F')
            return true;
        return IsDigit(character);
    }

    bool StrayaCulture::IsAlphaNum(char character)
    {
        return IsAlpha(character) || IsDigit(character);
    }

    bool StrayaCulture::IsSpace(char character)
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
    
    char StrayaCulture::ToUpper(char character)
    {
        if (IsLower(character))
            return character -= 0x20;
        return character;
    }

    char StrayaCulture::ToLower(char character)
    {
        if (IsUpper(character))
            return character += 0x20;
        return character;
    }

}
