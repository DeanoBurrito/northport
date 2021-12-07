#include <FormatPrinter.h>
#include <Maths.h>
#include <StringCulture.h>

//for ParseFormatToken, easy way to check if we need to early exit once having read format.
#define RETURN_IF_INPUT_EXHAUSTED if (inputPos == inputMaxLength) return token;

namespace sl
{
    FormatToken FormatPrinter::ParseFormatToken()
    {
        if (inputBuffer[inputPos] == '%')
        {
            inputPos++;
            return FormatToken { .specifier = FormatSpecifier::Verbatim };
        }

        FormatToken token;
        
        //try parse flags
        switch (inputBuffer[inputPos])
        {
        case '-':
            token.flags = token.flags | FormatFlag::LeftJustified;
            inputPos++;
            break;
        case '+':
            token.flags = token.flags | FormatFlag::AlwaysShowSign;
            inputPos++;
            break;
        case ' ':
            token.flags = token.flags | FormatFlag::InsertSpaceIfNoSign;
            inputPos++;
            break;
        case '#':
            token.flags = token.flags | FormatFlag::UseAltConversion;
            inputPos++;
            break;
        case '0':
            token.flags = token.flags | FormatFlag::PadWithZeros;
            inputPos++;
            break;
        
        default:
            break;
        }

        RETURN_IF_INPUT_EXHAUSTED
        
        //try parse width
        size_t segmentStart = inputPos;
        while (StringCulture::current->IsDigit(inputBuffer[inputPos]))
            inputPos++;

        if (inputPos > segmentStart)
        {
            //TODO: support reading integers from strings
            token.minimumPrintWidth = 1;
        }
        else if (inputBuffer[inputPos] == '*')
        {
            token.minimumPrintWidth = PRINT_WIDTH_READ_FROM_INPUT;
            inputPos++;
        }
        else 
            token.minimumPrintWidth = PRINT_WIDTH_UNSPECIFIED;

        RETURN_IF_INPUT_EXHAUSTED

        //try parse precision
        token.precision = PRECISION_UNSPECIFIED;
        if (inputBuffer[inputPos] == '.')
        {
            inputPos++;
            segmentStart = inputPos;
            while (StringCulture::current->IsDigit(inputBuffer[inputPos]))
                inputPos++;
            
            if (inputPos > segmentStart)
            {
                //TODO: support reading integers from strings
                token.precision = 1;
            }
            else if (inputBuffer[inputPos] == '*')
            {
                token.precision = PRECISION_READ_FROM_INPUT;
                inputPos++;
            }
        }

        RETURN_IF_INPUT_EXHAUSTED

        //try parse length mod
        token.lengthMod = FormatLengthMod::None;
        switch (inputBuffer[inputPos]) 
        {
        case 'h':
        {   
            inputPos++;
            if (inputBuffer[inputPos] == 'h')
            {
                token.lengthMod = FormatLengthMod::HalfHalf; 
                inputPos++;
            }
            else
                token.lengthMod = FormatLengthMod::Half;
            break;
        }
        case 'l':
        {
            inputPos++;
            if (inputBuffer[inputPos] == 'l')
            {
                token.lengthMod = FormatLengthMod::LongLong;
                inputPos++;
            }
            else
                token.lengthMod = FormatLengthMod::Long;
            break;
        }

        default:
            token.lengthMod = FormatLengthMod::Unsupported;
        }

        RETURN_IF_INPUT_EXHAUSTED

        //parse conversion spec
        switch (inputBuffer[inputPos])
        {
        case 'i':
        case 'd':
            token.specifier = FormatSpecifier::SignedIntDecimal;
            inputPos++;
            break;

        case 'c':
            token.specifier = FormatSpecifier::SingleChar;
            inputPos++;
            break;

        case 's':
            token.specifier = FormatSpecifier::String;
            inputPos++;
            break;

        case 'o':
            token.specifier = FormatSpecifier::UnsignedIntOctal;
            inputPos++;
            break;

        case 'u':
            token.specifier = FormatSpecifier::UnsignedIntDecimal;
            inputPos++;
            break;

        case 'X':
            token.isBig = true;
        case 'x':
            token.specifier = FormatSpecifier::UnsignedIntHex;
            inputPos++;
            break;

        case 'F':
            token.isBig = true;
        case 'f':
            token.specifier = FormatSpecifier::FloatingPointDecimal;
            inputPos++;
            break;
        
        case 'E':
            token.isBig = true;
        case 'e':
            token.specifier = FormatSpecifier::FloatingPointExponent;
            inputPos++;
            break;

        case 'A':
            token.isBig = true;
        case 'a':
            token.specifier = FormatSpecifier::FloatingPointHexponent;
            inputPos++;
            break;

        case 'G':
            token.isBig = true;
        case 'g':
            token.specifier = FormatSpecifier::FloatingPointShortest;
            inputPos++;
            break;

        case 'n':
            token.specifier = FormatSpecifier::GetCharsWritten;
            inputPos++;
            break;
        
        case 'p':
            token.specifier = FormatSpecifier::CustomImplementation;
            inputPos++;
            break;

        // ---- CUSTOM ---- //
        case 'B':
            token.isBig = true;
        case 'b':
            token.specifier = FormatSpecifier::CUSTOM_Bool;
            inputPos++;
            break;
        }

        return token;
    }

    void FormatPrinter::PrintFormatToken(FormatToken token, va_list args)
    {
        //TODO: actually respect the length remaining
        char* buff = new char[5];
        sl::memcopy("TOKEN", buff, 5);

        outputBuffers.Append(buff);
        outputPos += 5;
        bufferLengths.PushBack(5);
    }

    void FormatPrinter::FormatAll(va_list args)
    {
        inputMaxLength = sl::memfirst(inputBuffer, 0, 0);

        size_t cleanTextStart = 0;
        while (inputPos < inputMaxLength)
        {
            cleanTextStart = inputPos;
            while (inputBuffer[inputPos] != '%')
            {
                if (inputPos == inputMaxLength)
                    break; //we've reached the end of the string

                inputPos++;
            }
            
            //we hit a format token, copy regular to ouput, consume leading '%' and parse token
            const size_t sectionSize = inputPos - cleanTextStart;
            if (sectionSize > 0)
            {
                char* newSection = new char[sectionSize];
                sl::memcopy(inputBuffer, cleanTextStart, newSection, 0, sectionSize);

                outputBuffers.Append(newSection);
                outputPos += sectionSize;
                bufferLengths.PushBack(sectionSize);
            }

            if (inputPos == inputMaxLength)
                return; //we've reached the end, not a format token

            inputPos++;
            FormatToken token = ParseFormatToken();
            PrintFormatToken(token, args);
        }
    }

    size_t FormatPrinter::CharsWritten() const
    { return outputPos; }

    const char* FormatPrinter::GetOutput()
    {
        char* buffer = new char[outputPos + 1];
        size_t bufferPos = 0;
        size_t bufferIndex = 0;

        for (auto it = outputBuffers.Begin(); it != outputBuffers.End(); ++it)
        {
            sl::memcopy(*it, 0, buffer, bufferPos, bufferLengths[bufferIndex]);
            bufferPos += bufferLengths[bufferIndex];
            bufferIndex++;
        }

        bufferLengths.Clear();
        outputBuffers.Clear();

        buffer[bufferPos] = 0;
        return buffer;
    }

    void FormatPrinter::OutputToBuffer(char* buffer, size_t bufferLength)
    {

    }
}
