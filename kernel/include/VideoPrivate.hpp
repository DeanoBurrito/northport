#pragma once

#include <Video.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag VideoTag = NPK_MAKE_HEAP_TAG("Vido");
    constexpr size_t ColourCount = 8;
    constexpr size_t CsiArgCount = 4;

    using Pixel = uint32_t;

    struct TextRendererConfig
    {
        sl::Span<bool> font;
        sl::Vector2u fontSize;
        Colour colours[ColourCount];
        Colour brightColours[ColourCount];
        Colour background;
        Colour foreground;
    };

    struct TextRenderer;

    struct TextAdaptor
    {
        sl::FwdListHook hook;

        SimpleFramebuffer* framebuffer;
        TextRenderer* renderer;
        sl::Vector2u margin;
        sl::Vector2u scale;
        sl::Vector2u offset;

        Pixel dimCols[ColourCount];
        Pixel brightCols[ColourCount];
        Pixel foreground;
        Pixel background;
    };

    using TextAdaptorList = sl::FwdList<TextAdaptor, &TextAdaptor::hook>;

    struct Cell
    {
        uint8_t fgIndex;
        uint8_t bgIndex;
        char data;
    };

    struct PendingWrite
    {
        sl::Vector2u pos;
        Cell cell;
    };

    struct TextRenderer
    {
        LogSink sink;

        Mutex mutex;

        size_t pendingHead;
        sl::Span<PendingWrite> pending;
        sl::Span<PendingWrite*> pendingMap;
        sl::Span<Cell> cells;

        TextRendererConfig* config;
        TextAdaptorList adaptors;

        sl::Vector2u cursor;
        sl::Vector2u size;
        uint8_t fgIndex;
        uint8_t bgIndex;

        struct
        {
            unsigned args[CsiArgCount];
            uint8_t argIndex;
            bool inEscape;
            bool inControlSeq;
            bool inEscArg;
        } parser;
    };
}
