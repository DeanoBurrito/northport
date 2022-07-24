#pragma once

#include <String.h>
#include <stdarg.h>

/*  
C-Style printf format spec:
    A full printf specifier is made up of 6 parts.
    - First the '%' character, which indicates that the following sequence is not text, but rather a format specifier.
    - Then the optional flags:
        - '-' means to left justify the conversion (default is right)
        - '+' causes a number conversion to always show the sign.
        - ' ' pads with a leading space if no sign is needed.
        - '#' uses the alternative conversion.
        - '0' pads with leading zeros instead of spaces.
    - Then the optional print width, this is the minimum number of characters this conversion outputs, padded with spaces.
    - Then the optional precision. Indicated by a leading '.' and followed by digits. Both width and precision can accept '*', 
    which causes the value to read from the input args as an int, immediately before the conversion input.
    Precision is either characters to be printed (for integer numbers), bytes printed (for strings), 
    or digits after decimal point (for floating point numbers).
    - Then an optional length modifier, which changes the input type the conversion uses.
    - The last part is the conversion specifier itself, which is required:
        - '%' causes a verbatim '%' to be ouput.
        - 'c' outputs a single character.
        - 's' outputs a c-style character string. Precision is bytes printed.
        - 'd'/'i' outputs a signed decimal integer. Default precision is 1.
        - 'o' outputs an unsigned octal integer. Default precision is 1.
        - 'x'/'X' output an unsigned hex integer. Large X for large hex digits, lower case for lower case. Default precision is 1.
        - 'u' outputs an unsigned integer. Default precision is 1.
        - 'f'/'F' outputs a floating point number, default precision is 6.
        - 'e'/'E' outputs a floating point number, with exponent notaton. Default precision is 6.
        - 'a'/'A' like e/E, but encodes using hex instead of decimal. 
        - 'g'/'G' uses the shortest of either f/e.
        - 'n' writes the number of characters currently written to the pointer passed in.
        - 'p' is defined as implementation specified, and is ignored in this parser.

Northport specific:
        - 'b'/'B' outputs true/TRUE or false/FALSE depending on whether a number is non-zero.
        - 'U' outputs in binary units (KiB/MiB/GiB/TiB) and will scale and add the appropriate prefix. Precision is number of digits after the decimal place.
        - '#s' alternate conversion for string specifier. This is non-standard, but I find it to be useful. It will also consider the ASCII space character a string terminator.
*/

/*
    A quick note on the use of 'int ignored'. Because we're using code that was designed before c++ references were available,
    it wont process references as the last known arg properly. Therefore we need a POD argument to act as a guard.
    Currently it's unused, I might make use of it for flags or something in the future.
*/

namespace sl
{
    string FormatToString(const string& format, int ignored, ...);
    string FormatToString(string&& format, int ignored, ...);
    string FormatToStringV(const string& format, va_list args);

    size_t FormatToBuffer(void* buffer, size_t bufferLength, const string& format, int ignored, ...);
    size_t FormatToBuffer(void* buffer, size_t bufferLength, string&& format, int ignored, ...);
    size_t FormatToBufferV(void* buffer, size_t bufferLength, const string& format, va_list args);
}
