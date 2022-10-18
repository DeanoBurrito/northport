/*
    This file is derived from Limine, see the README in kernel/debug/terminal for details.
    License text is as follows:

    BSD 2-Clause License

    Copyright (c) 2019, 2020, 2021, 2022, mintsuki and contributors
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include <debug/TerminalImage.h>

namespace Npk::Debug 
{
    constexpr size_t TerminalTabSize = 8;
    constexpr size_t MaxEscValues = 16;

    #define DEFAULT_ANSI_COLOURS { 0x00000000, 0x00AA0000, 0x0000AA00, 0x00AA5500, 0x000000AA, 0x00AA00AA, 0x0000AAAA, 0x00AAAAAA }
    #define DEFAULT_ANSI_BRIGHT_COLOURS { 0x00555555, 0x00FF5555, 0x0055FF55, 0x00FFFF55, 0x005555FF, 0x00FF55FF, 0x0055FFFF, 0x00FFFFFF }

    struct Vector2
    {
        size_t x;
        size_t y;
    };

    struct GTFramebuffer
    {
        uintptr_t address;
        Vector2 size;
        uint64_t pitch;
    };

    struct GTFont
    {
        uintptr_t address;
        Vector2 size;
        uint8_t spacing;
    };

    struct GTStyle
    {
        uint32_t colours[8];
        uint32_t brightColours[8];
        uint32_t background;
        uint32_t foreground;
        uint16_t margin;
        uint16_t fontSpacing;
    };

    struct GTBackground
    {
        GTImage* background;
        uint32_t backdrop;
    };

    struct GTChar
    {
        uint32_t c;
        uint32_t fg;
        uint32_t bg;
    };

    struct GTQueueItem
    {
        Vector2 pos;
        GTChar c;
    };

    struct TerminalContext
    {
        bool controlSequence;
        bool csi;
        bool escape;
        bool rrr;
        bool discardNext;
        bool bold;
        bool cursorVisible;
        bool scrollEnabled;
        size_t escapeOffset;
        size_t escValuesIndex;
        size_t currentPrimary;
        size_t scrollTopMargin;
        size_t scrollBottomMargin;
        uint32_t escapeValues[MaxEscValues];
        uint32_t textFg;
        uint32_t textBg;
        Vector2 cursor;
    };

    class Terminal
    {
    private:
        TerminalContext context;
        GTFramebuffer fb;
        volatile uint32_t* fbAddr;
        bool initialized; //TODO: this is underused
        bool autoflush;

        Vector2 size;
        Vector2 fontSize;
        Vector2 offset;
        Vector2 oldCursor;

        size_t margin;
        size_t gridSize;
        size_t queueSize;
        size_t mapSize;

        const uint8_t* vgaFontBits;
        size_t vgaFontBoolSize;
        bool* vgaFontBool;

        GTChar* grid;
        GTQueueItem** map;
        GTQueueItem* queue;
        size_t queueIndex;

        uint32_t colours[8];
        uint32_t brightColours[8];
        uint32_t defaultFg;
        uint32_t defaultBg;
        
        GTImage* background;
        size_t bgCanvasSize;
        uint32_t* bgCanvas;

        void QueueOp(GTChar* c, size_t x, size_t y);
        void GenCanvasLoop(bool externalLoop, Vector2 start, Vector2 end);
        void GenerateCanvas();
        void DrawCursor();
        void PlotCharFast(GTChar* old, GTChar* c, size_t x, size_t y);
        void PlotChar(GTChar* c, size_t x, size_t y);

        void RawPutchar(uint8_t c);
        void Putchar(uint8_t c);
        void ParseSgr();
        void ParseControlSequence(uint8_t c);
        void ParseEscape(uint8_t c);

        void SetTextFg(size_t index);
        void SetTextBg(size_t index);
        void SetTextFgBright(size_t index);
        void SetTextBgBright(size_t index);
        bool ScrollDisable();
        void ScrollEnable();
        void Scroll();
        void ReverseScroll();
        void SwapPalette();

    public:
        bool Init(const GTStyle& style, const GTBackground& background);
        void Deinit();
        void Reinit();

        void Write(const char* str, size_t length);
        void Clear(bool move = false);
        void Flush();

        void EnableCursor();
        bool DisableCursor();
        void SetCursorPos(Vector2 pos);
        Vector2 GetCursorPos();
    };
}
