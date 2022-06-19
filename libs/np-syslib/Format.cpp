#include <Format.h>
#include <FormatPrinter.h>

namespace sl
{
    string FormatToString(const string& format, int ignored, ...)
    {
        va_list argsList;
        va_start(argsList, ignored);
        string str = FormatToStringV(format, argsList);
        va_end(argsList);

        return str;
    }

    string FormatToString(string&& format, int ignored, ...)
    {
        va_list argsList;
        va_start(argsList, ignored);
        string str = FormatToStringV(format, argsList);
        va_end(argsList);

        return str;
    }

    string FormatToStringV(const string& format, va_list args)
    {
        FormatPrinter printer(format.C_Str(), OUTPUT_LENGTH_DONT_CARE);
        printer.FormatAll(args);

        return printer.GetOutput();
    }

    size_t FormatToBuffer(void* buffer, size_t bufferLength, const string& format, int ignored, ...)
    {
        va_list argsList;
        va_start(argsList, ignored);
        size_t charsWritten = FormatToBufferV(buffer, bufferLength, format, argsList);
        va_end(argsList);
        return charsWritten;
    }

    size_t FormatToBuffer(void* buffer, size_t bufferLength, string&& format, int ignored, ...)
    {
        va_list argsList;
        va_start(argsList, ignored);
        size_t charsWritten = FormatToBufferV(buffer, bufferLength, format, argsList);
        va_end(argsList);
        return charsWritten;
    }

    size_t FormatToBufferV(void* buffer, size_t bufferLength, const string& format, va_list args)
    {
        FormatPrinter printer(format.C_Str(), bufferLength);
        printer.FormatAll(args);

        printer.OutputToBuffer({ buffer, bufferLength });
        return printer.CharsWritten();
    }
}
