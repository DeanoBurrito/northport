#include <Log.h>
#include <Panic.h>
#include <Locks.h>
#include <Memory.h>

namespace Kernel
{
    constexpr const char* LevelStrings[] = 
    {
        "[Info] ", "[Warning] ", "[Error] ", 
        "[Fatal] ", "[Verbose] ", "[Debug] ", 
        "" //yes, this is meant to be here.
    };

    constexpr const char* LevelStringsAnsiColours[] =
    {
        "\033[97m", "\033[93m", "\033[91m", 
        "\033[31m", "\033[90m", "\033[94m", 
        "\033[39m"
    };

    constexpr const char* AnsiColourResetStr = "\033[39m";

    //these are preset for blue = lowest byte, red = highest byte. If fb.isNotBGR is set, we'll invert these.
    //these are the 2 most common framebuffer types, and can suffice for now.
    constexpr uint32_t LevelStringGfxColours[] = 
    {
        (uint32_t)-1, 0xFF'B0'00, 0xD0'00'00,
        0xFF'88'40, 0x88'88'88, 0x44'88'FF,
        0
    };
    
    //defined in LogFont.cpp
    extern const uint8_t logFontData[];
    extern const size_t PanicImageWidth;
    extern const size_t PanicImageHeight;
    extern const uint8_t panicImage[];

    bool logDestsStatus[(unsigned)LogDest::EnumCount];
    bool logDestsColours[(unsigned)LogDest::EnumCount];

    LogFramebuffer logFb;
    size_t logFbLine;
    uint32_t logFbClearColour;
    
    bool fullLoggingAvail; //this is used for logf(), to determine if we can format text (if we have a heap yet).
    bool panicOnError;

    char debugconLock;
    char logFbLock;

    void RenderFramebufferLine(const char* str, size_t drawX, size_t drawY, uint32_t colour)
    {
        constexpr size_t fontWidth = 8;
        constexpr size_t fontHeight = 16;
        sl::NativePtr glyphBuffer = sl::NativePtr((uintptr_t)logFontData).As<void>(4);

        for (size_t i = 0; str[i] != 0; i++)
        {
            uint8_t* glyph = glyphBuffer.As<uint8_t>(str[i] * 16);

            for (size_t y = 0; y < fontHeight; y++)
            {
                for (size_t x = 0; x < fontWidth; x++)
                {
                    if ((*glyph & (0b10000000 >> x)) == 0)
                        continue;
                    
                    const size_t dx = (drawX + i) * fontWidth + x;
                    const size_t dy = drawY * fontHeight + y;
                    sl::MemWrite<uint32_t>(logFb.base.As<void>((dx * logFb.bpp / 8) + (dy * logFb.stride)), colour & logFb.pixelMask);
                }
                glyph++;
            }
        }
    }

    void RenderPanicImage()
    {
        const size_t left = logFb.width - (PanicImageWidth + 16);
        const size_t top = logFb.height - (PanicImageHeight + 16);
        const size_t bytesPerPixel = logFb.bpp / 8;

        for (size_t y = 0; y < PanicImageHeight; y++)
        {
            for (size_t x = 0; x < PanicImageWidth; x++)
            {
                const size_t imageIndex = y * PanicImageWidth + x;
                if ((panicImage[imageIndex / 8] & (1 << (imageIndex % 8))) == 0)
                    sl::MemWrite(logFb.base.As<void>((top + y) * logFb.stride + (left + x) * bytesPerPixel), 0xFFFFFF);
            }
        }
    }

    void LoggingInitEarly()
    {
        //make sure all logging is disabled at startup
        for (unsigned i = 0; i < (unsigned)LogDest::EnumCount; i++)
        {
            logDestsStatus[i] = false;
            logDestsColours[i] = false;
        }

        fullLoggingAvail = false;
        panicOnError = false;
        logFbLine = 0;
        logFbClearColour = 0;
        logFb.base = nullptr;

        sl::SpinlockRelease(&debugconLock);
        sl::SpinlockRelease(&logFbLock);
    }

    void LoggingInitFull()
    {
        fullLoggingAvail = true;
    }

    void LogEnableDest(LogDest dest, bool enabled)
    {
        if ((unsigned)dest < (unsigned)LogDest::EnumCount)
            logDestsStatus[(unsigned)dest] = enabled;
    }

    void LogEnableColours(LogDest dest, bool enabled)
    {
        if ((unsigned)dest < (unsigned)LogDest::EnumCount)
            logDestsColours[(unsigned)dest] = enabled;
    }

    void SetPanicOnLogError(bool yes)
    {
        panicOnError = yes;
    }

    void SetLogFramebuffer(LogFramebuffer fb)
    { 
        sl::ScopedSpinlock scopeLock(&logFbLock);
        logFb = fb;
    }

    void LogFramebufferClear(uint32_t pixel)
    {
        if (logFb.base.ptr == nullptr)
            return;

        for (size_t i = 0; i < logFb.height; i++)
            sl::memsetT(logFb.base.As<void>(i * logFb.stride), pixel, logFb.width);
        
        logFbClearColour = pixel;
        logFbLine = 0;
    }
    
    void Log(const char* message, LogSeverity level)
    {   
        const size_t levelIndex = (size_t)level;
        
        for (unsigned i = 0; i < (unsigned)LogDest::EnumCount; i++)
        {
            if (!logDestsStatus[i])
                continue;
            
            InterruptLock intLock;
            switch ((LogDest)i)
            {
            case LogDest::DebugCon:
                {
                    auto PrintToE9 = [](const char* str)
                    {
                        for (size_t index = 0; str[index] != 0; index++)
                            PortWrite8(PORT_DEBUGCON, str[index]);
                    };

                    sl::ScopedSpinlock scopeLock(&debugconLock);

                    if (logDestsColours[(size_t)LogDest::DebugCon])
                        PrintToE9(LevelStringsAnsiColours[levelIndex]);
                    PrintToE9(LevelStrings[levelIndex]);
                    if (logDestsColours[(size_t)LogDest::DebugCon])
                        PrintToE9(AnsiColourResetStr);
                    
                    PrintToE9(message);

                    PortWrite8(PORT_DEBUGCON, '\n');
                    PortWrite8(PORT_DEBUGCON, '\r');
                    break;
                }

            case LogDest::FramebufferOverwrite:
                {
                    if (logFb.base.ptr == nullptr)
                        break;
                    
                    sl::ScopedSpinlock scopeLock(&logFbLock);

                    //clear the line we're about to draw on
                    constexpr size_t fontHeight = 16;
                    for (size_t i = 0; i < fontHeight; i++)
                        sl::memsetT<uint32_t>(logFb.base.As<void>((logFbLine * fontHeight + i) * logFb.stride), logFbClearColour, logFb.width);

                    if (!logDestsColours[(size_t)LogDest::FramebufferOverwrite])
                        RenderFramebufferLine(LevelStrings[levelIndex], 0, logFbLine, (uint32_t)-1);
                    else
                        RenderFramebufferLine(LevelStrings[levelIndex], 0, logFbLine, LevelStringGfxColours[levelIndex]);
                    
                    RenderFramebufferLine(message, 10, logFbLine, (uint32_t)-1);
                    logFbLine++;
                    if (logFbLine >= logFb.height / fontHeight)
                        logFbLine = 0;
                    break;
                }

            default:
                break;
            }
        }

        if (level == LogSeverity::Fatal || (panicOnError && level == LogSeverity::Error))
            Panic(message);
    }
}
