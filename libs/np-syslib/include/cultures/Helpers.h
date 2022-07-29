#pragma once

#include <String.h>
#include <stddef.h>

namespace sl::Cultures
{
    /*
        This file contains some templated functions that are useful for string <-> integer conversions.
        Given that a lot of languages share the arabic numbering system, they can simply use these templates.
        If you're implementing a culture that uses a different numbering system, you will need to write
        these functions yourself.

        These functions are intended for internal syslib use, for actually converting numbers for normal use
        it's preferable to use the provided functions, rather than instantiate new templates.
    */

    template<typename IntType>
    inline IntType ConvertStringToInt(const sl::String& str, size_t start, size_t base)
    {
        IntType out = 0;

        if (base < 2 || base > 32)
            return 0;
        
        bool negative = false;
        if (str[start] == '-')
        {
            negative = true;
            start++;
        }

        const char maxConvertedChar = (char)base - 10 + 'A';

        for (size_t i = start; i < str.Size(); i++)
        {
            out *= base;

            char c = str[i];
            if (c >= 'a' && c <= 'z')
                c -= 0x20; //bring lowercase letters into uppercase range
            
            if (c >= 'A' && c <= maxConvertedChar)
                out += c - 'A' + 10;
            else if (c >= '0' && c <= '9')
                out += c - '0';
            else
            {
                //not a digit, undo the last multiply and return.
                out /= base;
                break; 
            }
        }

        if (negative)
            out = -out;

        return out;
    }

    template<typename IntType>
    inline sl::String ConvertIntToString(IntType num, size_t base, size_t bufferLength)
    {
        if (base < 2 || base > 32)
            return {};
        
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
            num /= base;

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

        return sl::String(buffer, true);
    }
}
