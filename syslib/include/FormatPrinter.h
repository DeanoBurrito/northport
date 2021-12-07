#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <containers/LinkedList.h>
#include <containers/Vector.h>

namespace sl
{
    enum class FormatFlag
    {
        None = 0,

        LeftJustified = 1 << 0,
        AlwaysShowSign = 1 << 1,
        InsertSpaceIfNoSign = 1 << 2,
        UseAltConversion = 1 << 3,
        PadWithZeros = 1 << 4,
    };

    constexpr FormatFlag operator |(const FormatFlag& a, const FormatFlag& b)
    { return static_cast<FormatFlag>((unsigned)a | (unsigned)b); }

    constexpr FormatFlag operator &(const FormatFlag& a, const FormatFlag& b)
    { return static_cast<FormatFlag>((unsigned)a & (unsigned)b); }

    constexpr static uint64_t PRINT_WIDTH_READ_FROM_INPUT = (uint64_t)-1;
    constexpr static uint64_t PRINT_WIDTH_UNSPECIFIED = (uint64_t)-2;
    constexpr static uint64_t PRECISION_READ_FROM_INPUT = (uint64_t)-1;
    constexpr static uint64_t PRECISION_UNSPECIFIED = (uint64_t)-2;

    enum class FormatLengthMod
    {
        None = 0,

        Half,
        HalfHalf,
        Long,
        LongLong,

        Unsupported,
    };

    enum class FormatSpecifier
    {
        Unsupported,
        Verbatim,

        SingleChar,
        String,
        SignedIntDecimal,
        UnsignedIntOctal,
        UnsignedIntHex,
        UnsignedIntDecimal,
        FloatingPointDecimal,
        FloatingPointExponent,
        FloatingPointHexponent,
        FloatingPointShortest,
        GetCharsWritten,
        CustomImplementation,

        CUSTOM_Bool,
    };

    struct FormatToken
    {
        FormatFlag flags = FormatFlag::None;
        uint64_t minimumPrintWidth = PRINT_WIDTH_UNSPECIFIED;
        uint64_t precision = PRECISION_UNSPECIFIED;
        FormatLengthMod lengthMod = FormatLengthMod::None;
        bool isBig = false;
        FormatSpecifier specifier = FormatSpecifier::Unsupported;
    };

    /*
        This is a monster of a class, it handles a lot of the printf spec, and some custom formats too.
        See Format.h for what's supported.

        If you're using this for your own code, I'd suggest using the functions in Format.h, but using this directly is not too hard.
        The single constructor takes the c-style formatting string, and an optional limit on the output (0 means no output,means no limit)
        for no limit use the FormatPrinter::OUTPUT_LENGTH_DONT_CARE constant).
        To actually process the input string, call FormatAll() with the va_list of args required, this MUST match what is expected.
        FormatAll() does a single pass over the input text, and creates a linked list of plain text, and formatted data.
        To get a single string output, use one of the output functions.
        GetOutput() returns an exclusive (read: you now own this pointer, see the [[nodiscard]] attribute).
        OutputToBuffer() takes an existing buffer, and performs the list collapse into that instead of an internal one.
    */
    class FormatPrinter
    {
    private:
        const char* inputBuffer;
        size_t inputPos;
        size_t inputMaxLength;
        size_t outputPos;
        size_t outputMaxLength;
        sl::LinkedList<char*> outputBuffers;
        sl::Vector<size_t> bufferLengths;

        FormatToken ParseFormatToken();
        void PrintFormatToken(FormatToken token, va_list args);
        
    public:
        constexpr static size_t OUTPUT_LENGTH_DONT_CARE = (size_t)-1;
        
        FormatPrinter() = delete;
        FormatPrinter(const char* formatString, size_t outputLength) 
            : inputBuffer(formatString), inputPos(0), inputMaxLength(0), outputPos(0), outputMaxLength(outputLength)
        {}

        FormatPrinter(const FormatPrinter&) = delete;
        FormatPrinter& operator=(const FormatPrinter&) = delete;
        FormatPrinter(FormatPrinter&&) = delete;
        FormatPrinter& operator=(FormatPrinter&&) = delete;

        void FormatAll(va_list args);
        size_t CharsWritten() const;
        [[nodiscard]] 
        const char* GetOutput();
        void OutputToBuffer(char* buffer, size_t bufferLength);
    };
}
