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
        while (IsDigit(inputBuffer[inputPos]))
            inputPos++;

        if (inputPos > segmentStart)
            token.minimumPrintWidth = GetUInt64(inputBuffer, segmentStart, Base::Decimal);
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
            while (IsDigit(inputBuffer[inputPos]))
                inputPos++;
            
            if (inputPos > segmentStart)
                token.precision = GetUInt64(inputBuffer, segmentStart, Base::Decimal);
            else if (inputBuffer[inputPos] == '*')
            {
                token.precision = PRECISION_READ_FROM_INPUT;
                inputPos++;
            }
            else
            {
                token.precision = PRECISION_DEFAULT;
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
        
        case 'U':
            token.specifier = FormatSpecifier::CUSTOM_Units;
            inputPos++;
            break;


        default: //just consume something, even if its an unknown, to avoid infinite loops.
            inputPos++;
            break;
        }

        return token;
    }

#define OUTPUT_SIMPLE_TOKEN(text, len) { char* str = new char[len]; sl::memcopy(text, str, len); outputBuffers.Append(str); outputPos += (len); bufferLengths.PushBack(len); }

    void FormatPrinter::PrintFormatToken(FormatToken token, va_list args)
    {
        auto ApplyPadding = [&](bool padLeft, char padChar, size_t padLength)
        {
            char* padBuff = new char[padLength];
            for (size_t i = 0; i < padLength; i++)
                padBuff[i] = padChar; //use a for loop instead of memset, since a char may be more than 1 byte
            
            outputBuffers.Append(padBuff);
            bufferLengths.PushBack(padLength);
            outputPos += padLength;
        };
        
        //check if width or precision need to read their values from the input
        if (token.minimumPrintWidth == PRINT_WIDTH_READ_FROM_INPUT)
            token.minimumPrintWidth = (uint64_t)va_arg(args, int);
        if (token.precision == PRECISION_READ_FROM_INPUT)
            token.precision = (uint64_t)va_arg(args, int);

        //TODO: handle format flags, precision, width and length mods.
        //TODO: actually respect the length remaining in the output buffer

        size_t formatBase = 0;
        switch (token.specifier)
        {
        case FormatSpecifier::Unsupported:
            OUTPUT_SIMPLE_TOKEN(Literals[4], LiteralSizes[4]);
            break;

        case FormatSpecifier::Verbatim:
            OUTPUT_SIMPLE_TOKEN(Literals[5], LiteralSizes[5]);
            break;

        case FormatSpecifier::SingleChar:
            {
                char* str = new char[1];
                str[0] = va_arg(args, int);

                outputBuffers.Append(str);
                outputPos++;
                bufferLengths.PushBack(1);
                break;
            }

        case FormatSpecifier::String:
            {
                char* source = va_arg(args, char*);
                if (token.precision == PRECISION_DEFAULT) 
                    break;
                
                size_t sourceLength = token.precision;
                if (sl::EnumHasFlag(token.flags, FormatFlag::UseAltConversion))
                    sourceLength = sl::min(sourceLength, sl::memfirst(source, ' ', token.precision));
                else
                    sourceLength = sl::min(sourceLength, sl::memfirst(source, 0, token.precision));

                char* str = nullptr;
                if (sourceLength > 0)
                    str= new char[sourceLength];
                sl::memcopy(source, str, sourceLength);

                outputBuffers.Append(str);
                outputPos += sourceLength;
                bufferLengths.PushBack(sourceLength);
                break;
            }

        case FormatSpecifier::SignedIntDecimal:
            {
                int64_t source;
                switch (token.lengthMod)
                {
                case FormatLengthMod::Long:
                    source = va_arg(args, long);
                    break;
                case FormatLengthMod::LongLong:
                    source = va_arg(args, long long);
                    break;
                default:
                    source = va_arg(args, int);
                    break;
                }

                string strValue = ToString(source, Base::Decimal);
                if (sl::EnumHasFlag(token.flags, FormatFlag::PadWithZeros))
                {
                    const size_t padLimit = IntStringLength(INT64_MIN, Base::Decimal);
                    if (padLimit > strValue.Size())
                        ApplyPadding(sl::EnumHasFlag(token.flags, FormatFlag::LeftJustified), '0', padLimit - strValue.Size());
                }

                outputPos += strValue.Size();
                bufferLengths.PushBack(strValue.Size());
                outputBuffers.Append(strValue.DetachBuffer());
                break;
            }

        case FormatSpecifier::UnsignedIntOctal:
            if (formatBase == 0)
                formatBase = Base::Octal;
        case FormatSpecifier::UnsignedIntHex:
            if (formatBase == 0)
                formatBase = Base::Hex;
        case FormatSpecifier::UnsignedIntDecimal:
            {
                if (formatBase == 0)
                    formatBase = Base::Decimal;

                uint64_t source;
                uint64_t sourcePad;
                switch (token.lengthMod)
                {
                case FormatLengthMod::Long:
                    source = va_arg(args, unsigned long);
                    sourcePad = (unsigned long)-1;
                    break;
                case FormatLengthMod::LongLong:
                    source = va_arg(args, unsigned long long);
                    sourcePad = (unsigned long long)-1;
                    break;
                case FormatLengthMod::Half:
                    source = va_arg(args, unsigned int);
                    sourcePad = (unsigned short)-1;
                    break;
                case FormatLengthMod::HalfHalf:
                    source = va_arg(args, unsigned int);
                    sourcePad = (unsigned char)-1;
                    break;
                default:
                    source = va_arg(args, unsigned int);
                    sourcePad = (unsigned int)-1;
                    break;
                }

                string strValue = ToString(source, formatBase);
                if (sl::EnumHasFlag(token.flags, FormatFlag::PadWithZeros))
                {
                    const size_t padLimit = UIntStringLength(sourcePad, formatBase);
                    if (padLimit > strValue.Size())
                        ApplyPadding(sl::EnumHasFlag(token.flags, FormatFlag::LeftJustified), '0', padLimit - strValue.Size());
                }

                outputPos += strValue.Size();
                bufferLengths.PushBack(strValue.Size());
                outputBuffers.Append(strValue.DetachBuffer());
                break;
            }

        case FormatSpecifier::FloatingPointDecimal:
        case FormatSpecifier::FloatingPointExponent:
        case FormatSpecifier::FloatingPointHexponent:
        case FormatSpecifier::FloatingPointShortest:
        case FormatSpecifier::GetCharsWritten:
        case FormatSpecifier::CustomImplementation:
            OUTPUT_SIMPLE_TOKEN(Literals[4], LiteralSizes[4]);
            break;

        case FormatSpecifier::CUSTOM_Bool:
            {
                unsigned source = va_arg(args, unsigned);
                size_t literalOffset = source ? 1 : 0;
                literalOffset += token.isBig ? 2 : 0;
                OUTPUT_SIMPLE_TOKEN(Literals[literalOffset], LiteralSizes[literalOffset]);
                break;
            }

        case FormatSpecifier::CUSTOM_Units:
            {
                size_t unitIndex = 0;
                uint64_t source;
                switch (token.lengthMod)
                {
                case FormatLengthMod::Long:
                    source = va_arg(args, unsigned long);
                    break;
                case FormatLengthMod::LongLong:
                    source = va_arg(args, unsigned long long);
                    break;
                default:
                    source = va_arg(args, unsigned int);
                    break;
                }

                uint64_t minorUnits = 0;
                uint64_t majorUnits = 0;
                while (source > 0)
                {
                    unitIndex++;
                    minorUnits = majorUnits;
                    majorUnits = source;
                    source /= KB;
                }
                if (unitIndex > 0)
                    unitIndex--;

                string strValue = ToString(majorUnits, Base::Decimal);
                outputPos += strValue.Size();
                bufferLengths.PushBack(strValue.Size());
                outputBuffers.Append(strValue.DetachBuffer());
                
                minorUnits = minorUnits % KB;
                if (minorUnits > 0)
                {
                    OUTPUT_SIMPLE_TOKEN(Literals[6], LiteralSizes[6]);

                    strValue = ToString(minorUnits, Base::Decimal);
                    outputPos += strValue.Size();
                    bufferLengths.PushBack(strValue.Size());
                    outputBuffers.Append(strValue.DetachBuffer());
                }

                //output the unit as a simple token
                OUTPUT_SIMPLE_TOKEN(Literals[7 + unitIndex], LiteralSizes[7 + unitIndex]);
            }
        }
    }

    //TODO: would be nice to replace a lot of the random heap allocations with stack allocs here.
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

    sl::String FormatPrinter::GetOutput()
    {
        const size_t finalBufferSize = outputPos < outputMaxLength ? outputPos : outputMaxLength;
        
        char* buffer = new char[finalBufferSize + 1];
        OutputToBuffer({ buffer, finalBufferSize + 1 });
        return sl::String(buffer, true);
    }

    void FormatPrinter::OutputToBuffer(sl::BufferView buffer)
    {
        const size_t finalLength = sl::min(buffer.length, outputPos);

        size_t bufferPos = 0;
        size_t bufferIndex = 0;

        for (auto it = outputBuffers.Begin(); it != outputBuffers.End(); ++it)
        {
            sl::memcopy(*it, 0, buffer.base.ptr, bufferPos, sl::min(bufferLengths[bufferIndex], finalLength - bufferPos));
            bufferPos += bufferLengths[bufferIndex];
            bufferIndex++;
        }

        buffer.base.As<uint8_t>()[bufferPos] = 0;
    }
}
