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

#include <boot/LimineTags.h>
#include <debug/Terminal.h>
#include <Memory.h>

namespace Npk::Debug
{
    using FixedP6 = size_t;

    [[gnu::always_inline]] 
    inline size_t FromFixedP(FixedP6 value)
    { return value / 64; }

    [[gnu::always_inline]]
    inline FixedP6 ToFixedP(size_t value)
    { return value * 64; }
    
    asm("FontBegin: \n .incbin \"../misc/TerminalFont\" \n FontEnd:");
    extern uint8_t FontBegin[] asm("FontBegin");
    extern uint8_t FontEnd[] asm("FontEnd");

#ifdef NP_INCLUDE_TERMINAL_BG
    asm("\
    .section .rodata \n\
    .balign 0x10 \n\
    BackgroundBegin: \n\
        .incbin \"../misc/TerminalBg.qoi\" \n\
    BackgroundEnd: \n \
    .previous \n \
    ");

    extern uint8_t BackgroundBegin[] asm("BackgroundBegin");
    extern uint8_t BackgroundEnd[] asm("BackgroundEnd");

    GTImage termBg;
#endif

    void Terminal::QueueOp(GTChar* c, size_t x, size_t y)
    {
        auto CompareChar = [](const GTChar& a, const GTChar& b)
        {
            return !(a.c != b.c || a.bg != b.bg || a.fg != b.fg);
        };
        
        if (x >= size.x || y >= size.y)
            return;

        const size_t index = y * size.x + x;
        GTQueueItem* q = map[index];

        if (q == nullptr)
        {
            if (CompareChar(grid[index], *c))
                return;
            q = &queue[queueIndex++];
            q->pos.x = x;
            q->pos.y = y;
            map[index] = q;
        }
        q->c = *c;
    }

    void Terminal::GenCanvasLoop(bool externalLoop, Vector2 start, Vector2 end)
    {
        auto Blend = [&](uint32_t original)
        {
            union Pixel
            {
                uint32_t squish;
                struct { uint8_t b, g, r, a; };
            };

            const Pixel bg { .squish = defaultBg };
            const Pixel orig  { .squish = original };
            const unsigned alpha = 255 - bg.a;
            const unsigned inverseAlpha = bg.a + 1;

            const uint8_t r = (uint8_t)((alpha * bg.r + inverseAlpha * orig.r) / 256);
            const uint8_t g = (uint8_t)((alpha * bg.g + inverseAlpha * orig.g) / 256);
            const uint8_t b = (uint8_t)((alpha * bg.b + inverseAlpha * orig.b) / 256);

            return Pixel{ .b = b, .g = g, .r = r, .a = 0}.squish;
        };
        
        const uint8_t* img = background->data;
        const size_t imageWidth = background->width;
        const size_t imageHeight = background->height;
        const size_t imageStride = background->stride;
        const size_t colsize = background->bpp / 8;

        for (size_t y = start.y; y < end.y; y++)
        {
            const size_t imgY = (y * imageHeight) / fb.size.y;
            const size_t off = imageStride * imgY;
            const size_t canvasOffset = fb.size.x * y;
            const size_t fbOffset = fb.pitch / 4 * y;
            const FixedP6 ratio = ToFixedP(imageWidth) / fb.size.x;

            FixedP6 imgX = ratio * start.x;
            for (size_t x = start.x; x < end.x; x++)
            {
                const uint32_t pixel = *((uint32_t*)(img + FromFixedP(imgX) * colsize + off));
                const uint32_t i = externalLoop ? pixel : Blend(pixel);

                bgCanvas[canvasOffset + x] = i; 
                fbAddr[fbOffset + x] = i;
                imgX += ratio;
            }
        }
    }

    void Terminal::GenerateCanvas()
    {
        auto PlotPixel = [=](size_t x, size_t y, uint32_t hex)
        {
            if (x >= fb.size.x || y >= fb.size.y)
                return;

            const size_t fbi = x + (fb.pitch / sizeof(uint32_t)) * y;
            fbAddr[fbi] = hex;
        };

        if (background != nullptr)
        {
            GenCanvasLoop(true, { 0, 0 }, { fb.size.x, margin });
            GenCanvasLoop(true, { 0, fb.size.y - margin }, { fb.size.x, fb.size.y });
            GenCanvasLoop(true, { 0, margin }, { margin, fb.size.y - margin });
            GenCanvasLoop(true, { fb.size.x - margin, margin }, { fb.size.x, fb.size.y - margin });
            GenCanvasLoop(false, { margin, margin }, { fb.size.x - margin, fb.size.y - margin });
        }
        else
        {
            for (size_t y = 0; y < fb.size.y; y++)
            {
                for (size_t x = 0; x < fb.size.x; x++)
                {
                    bgCanvas[y * fb.size.x + x] = defaultBg;
                    PlotPixel(x, y, defaultBg);
                }
            }
        }
    }

    void Terminal::DrawCursor()
    {
        const size_t idx = context.cursor.x + context.cursor.y * size.x;
        GTQueueItem* q = map[idx];
        GTChar c = q == nullptr ? grid[idx] : q->c;

        const uint32_t tmp = c.fg;
        c.fg = c.bg;
        c.bg = tmp;
        PlotChar(&c, context.cursor.x, context.cursor.y);
        if (q != nullptr)
        {
            grid[idx] = q->c;
            map[idx] = nullptr;
        }
    }

    void Terminal::PlotCharFast(GTChar* old, GTChar* c, size_t x, size_t y)
    {
        if (x >= size.x || y >= size.y)
            return;

        x = offset.x + x * fontSize.x;
        y = offset.y + y * fontSize.y;
        const bool* newGlyph = &vgaFontBool[c->c * fontSize.y * fontSize.x];
        const bool* oldGlyph = &vgaFontBool[old->c * fontSize.y * fontSize.x];

        for (size_t gy = 0; gy < fontSize.y; gy++)
        {
            volatile uint32_t* fbLine = fbAddr + x + (y + gy) * (fb.pitch / 4);
            const uint32_t* canvasLine = bgCanvas + x + (y + gy) * fb.size.x;

            for (size_t fx = 0; fx < fontSize.x; fx++)
            {
                const bool oldPixel = oldGlyph[gy * fontSize.x + fx];
                const bool newPixel = newGlyph[gy * fontSize.x + fx];
                if (oldPixel == newPixel)
                    continue;

                const uint32_t bg = c->bg == 0xFFFFFFFF ? canvasLine[fx] : c->bg;
                const uint32_t fg = c->fg == 0xFFFFFFFF ? canvasLine[fx] : c->fg;
                fbLine[fx] = newPixel ? fg : bg;
            }
        }
    }

    void Terminal::PlotChar(GTChar* c, size_t x, size_t y)
    {
        if (x >= size.x || y >= size.y)
            return;

        x = offset.x + x * fontSize.x;
        y = offset.y + y * fontSize.y;
        const bool* glyph = &vgaFontBool[c->c * fontSize.y * fontSize.x];

        for (size_t gy = 0; gy < fontSize.y; gy++)
        {
            volatile uint32_t* fbLine = fbAddr + x + (y + gy) * (fb.pitch / 4);
            const uint32_t* canvasLine = bgCanvas + x + (y + gy) * fb.size.x;

            for (size_t fx = 0; fx < fontSize.x; fx++)
            {
                const bool draw = glyph[gy * fontSize.x + fx];
                const uint32_t bg = c->bg == 0xFFFFFFFF ? canvasLine[fx] : c->bg;
                const uint32_t fg = c->fg == 0xFFFFFFFF ? canvasLine[fx] : c->fg;
                fbLine[fx] = draw ? fg : bg;
            }
        }
    }

    void Terminal::RawPutchar(uint8_t c)
    {
        if (!initialized) 
            return;

        GTChar ch { c, context.textFg, context.textBg };
        QueueOp(&ch, context.cursor.x++, context.cursor.y);
        if (context.cursor.x == size.x && (context.cursor.y < context.scrollBottomMargin - 1 || context.scrollEnabled))
        {
            context.cursor.x = 0;
            context.cursor.y++;
        }
        if (context.cursor.y == context.scrollBottomMargin)
        {
            context.cursor.y--;
            Scroll();
        }
    }

    void Terminal::Putchar(uint8_t c)
    {
        if (!initialized)
            return;

        if (context.discardNext)
        {
            context.discardNext = false;
            context.escape = false;
            context.csi = false;
            context.controlSequence = false;
            return;
        }

        if (context.escape == true)
        {
            ParseEscape(c);
            return;
        }

        const Vector2 cursor = GetCursorPos();
        switch (c)
        {
            case 0x00:
            case 0x7F:
                return;
            case 0x9B:
                context.csi = true;
                // FALLTHRU
            case '\e':
                context.escapeOffset = 0;
                context.escape = true;
                return;
            case '\t':
                if ((cursor.x / TerminalTabSize + 1) >= size.x)
                {
                    SetCursorPos({ size.x - 1, cursor.y });
                    return;
                }
                SetCursorPos({ (cursor.x / TerminalTabSize + 1) * TerminalTabSize, cursor.y });
                return;
            case 0x0b:
            case 0x0c:
            case '\n':
                if (cursor.y == context.scrollBottomMargin - 1)
                {
                    Scroll();
                    SetCursorPos({ 0, cursor.y });
                }
                else 
                    SetCursorPos({ 0, cursor.y + 1 });
                return;
            case '\b':
                SetCursorPos({ cursor.x - 1, cursor.y });
                return;
            case '\r':
                SetCursorPos({ 0, cursor.y });
                return;
        }

        RawPutchar(c);
    }

    void Terminal::ParseSgr()
    {
        size_t i = 0;

        if (context.escValuesIndex == 0) 
            goto def;

        for (; i < context.escValuesIndex; i++)
        {
            if (context.escapeValues[i] == 0)
            {
def:
                context.bold = false;
                context.currentPrimary = (size_t)(-1);
                context.textBg = defaultBg; 
                context.textFg = defaultFg;
                continue;
            }
            else if (context.escapeValues[i] == 1)
            {
                context.bold = true;
                if (context.currentPrimary != (size_t)(-1))
                    SetTextBgBright(context.currentPrimary);
                continue;
            }
            else if (context.escapeValues[i] == 22)
            {
                context.bold = false;
                if (context.currentPrimary != (size_t)(-1))
                    SetTextBg(context.currentPrimary);
                continue;
            }
            else if (context.escapeValues[i] >= 30 && context.escapeValues[i] <= 37)
            {
                constexpr size_t offset = 30;
                context.currentPrimary = context.escapeValues[i] - offset;

                if (context.bold)
                    SetTextFgBright(context.escapeValues[i] - offset);
                else
                    SetTextFg(context.escapeValues[i] - offset);

                continue;
            }
            else if (context.escapeValues[i] >= 40 && context.escapeValues[i] <= 47)
            {
                constexpr size_t offset = 40;
                if (context.bold)
                    SetTextBgBright(context.escapeValues[i] - offset);
                else
                    SetTextBg(context.escapeValues[i] - offset);
                continue;
            }
            else if (context.escapeValues[i] >= 90 && context.escapeValues[i] <= 97)
            {
                constexpr size_t offset = 90;
                context.currentPrimary = context.escapeValues[i] - offset;
                SetTextFgBright(context.escapeValues[i] - offset);
                continue;
            }
            else if (context.escapeValues[i] >= 100 && context.escapeValues[i] <= 107) {
                constexpr size_t offset = 100;
                SetTextBgBright(context.escapeValues[i] - offset);
                continue;
            }
            else if (context.escapeValues[i] == 39)
            {
                context.currentPrimary = (size_t)(-1);
                context.textFg = defaultFg;
                continue;
            }
            else if (context.escapeValues[i] == 49)
            {
                context.textBg = defaultBg;
                continue;
            }
            else if (context.escapeValues[i] == 7)
            {
                SwapPalette();
                continue;
            }
        }
    }

    void Terminal::ParseControlSequence(uint8_t c)
    {
        if (context.escapeOffset == 2 && c == '[')
        {
            context.discardNext = true;
            goto cleanup;
        }

        if (c >= '0' && c <= '9')
        {
            if (context.escValuesIndex == MaxEscValues)
                return;
            context.rrr = true;
            context.escapeValues[context.escValuesIndex] *= 10;
            context.escapeValues[context.escValuesIndex] += c - '0';
            return;
        }

        if (context.rrr == true)
        {
            context.escValuesIndex++;
            context.rrr = false;
            if (c == ';') 
                return;
        }
        else if (c == ';')
        {
            if (context.escValuesIndex == MaxEscValues)
                return;
            context.escapeValues[context.escValuesIndex] = 0;
            context.escValuesIndex++;
            return;
        }

        size_t esc_default;
        switch (c)
        {
            case 'J':
            case 'K':
            case 'q':
                esc_default = 0;
                break;
            default:
                esc_default = 1;
                break;
        }

        for (size_t i = context.escValuesIndex; i < MaxEscValues; i++)
            context.escapeValues[i] = esc_default;

        bool prevScrollEnabled; prevScrollEnabled = ScrollDisable();
        Vector2 cursor; cursor = GetCursorPos();

        switch (c) {
            case 'F':
                cursor.x = 0;
                // FALLTHRU
            case 'A':
            {
                if (context.escapeValues[0] > cursor.y) 
                    context.escapeValues[0] = cursor.y;
                
                const size_t origY = cursor.y;
                size_t destY = cursor.y - context.escapeValues[0];
                bool willBeInRegion = false;

                if ((context.scrollTopMargin >= destY && context.scrollTopMargin <= origY) 
                    || (context.scrollBottomMargin >= destY && context.scrollBottomMargin <= origY))
                    willBeInRegion = true;

                if (willBeInRegion && destY < context.scrollTopMargin)
                    destY = context.scrollTopMargin;

                SetCursorPos({ cursor.x, destY });
                break;
            }
            case 'E':
                cursor.x = 0;
                // FALLTHRU
            case 'e':
            case 'B':
            {
                if (cursor.y + context.escapeValues[0] > size.y - 1) 
                    context.escapeValues[0] = (size.y - 1) - cursor.y;
                
                const size_t origY = cursor.y;
                size_t destY = cursor.y + context.escapeValues[0];
                bool willBeInRegion = false;

                if ((context.scrollTopMargin >= origY && context.scrollTopMargin <= destY) 
                    || (context.scrollBottomMargin >= origY && context.scrollBottomMargin <= destY))
                    willBeInRegion = true;

                if (willBeInRegion && destY >= context.scrollBottomMargin)
                    destY = context.scrollBottomMargin - 1;

                SetCursorPos({ cursor.x, destY });
                break;
            }
            case 'a':
            case 'C':
                if (cursor.x + context.escapeValues[0] > size.x - 1)
                    context.escapeValues[0] = (size.x - 1) - cursor.x;
                SetCursorPos({ cursor.x + context.escapeValues[0], cursor.y });
                break;
            case 'D':
                if (context.escapeValues[0] > cursor.x)
                    context.escapeValues[0] = cursor.x;
                SetCursorPos({ cursor.x - context.escapeValues[0], cursor.y });
                break;
            case 'd':
                context.escapeValues[0] -= 1;

                if (context.escapeValues[0] >= size.y)
                    context.escapeValues[0] = size.y - 1;

                SetCursorPos({ cursor.x, context.escapeValues[0] });
                break;
            case 'G':
            case '`':
                context.escapeValues[0] -= 1;

                if (context.escapeValues[0] >= size.x)
                    context.escapeValues[0] = size.x - 1;

                SetCursorPos({ context.escapeValues[0], cursor.y });
                break;
            case 'H':
            case 'f':
                context.escapeValues[0] -= 1;
                context.escapeValues[1] -= 1;

                if (context.escapeValues[1] >= size.x)
                    context.escapeValues[1] = size.x - 1;

                if (context.escapeValues[0] >= size.y)
                    context.escapeValues[0] = size.y - 1;

                SetCursorPos({ context.escapeValues[1], context.escapeValues[0]});
                break;
            case 'J':
                switch (context.escapeValues[0])
                {
                    case 0:
                    {
                        const size_t rowsRemaining = size.y - (cursor.y + 1);
                        const size_t colsDiff = size.x - (cursor.x + 1);
                        const size_t toClear = rowsRemaining * size.x + colsDiff;

                        for (size_t i = 0; i < toClear; i++)
                            RawPutchar(' ');

                        SetCursorPos(cursor);
                        break;
                    }
                    case 1:
                    {
                        SetCursorPos({ 0, 0 });
                        bool b = false;
                        for (size_t yc = 0; yc < size.y; yc++)
                        {
                            for (size_t xc = 0; xc < size.x; xc++)
                            {
                                RawPutchar(' ');
                                if (xc == cursor.x && yc == cursor.y)
                                {
                                    SetCursorPos(cursor);
                                    b = true;
                                    break;
                                }
                            }
                            if (b == true) 
                                break;
                        }
                        break;
                    }
                    case 2:
                    case 3:
                        Clear(false);
                        break;
                }
                break;
            case 'm':
                ParseSgr();
                break;
            case 'K':
                switch (context.escapeValues[0])
                {
                    case 0:
                        for (size_t i = cursor.x; i < size.x; i++)
                            RawPutchar(' ');
                        SetCursorPos(cursor);
                        break;
                    case 1:
                        SetCursorPos({ 0, cursor.y });
                        for (size_t i = 0; i < cursor.x; i++)
                            RawPutchar(' ');
                        break;
                    case 2:
                        SetCursorPos({ 0, cursor.y });
                        for (size_t i = 0; i < size.x; i++)
                            RawPutchar(' ');
                        SetCursorPos(cursor);
                        break;
                }
                break;
            case 'r':
                context.scrollTopMargin = 0;
                context.scrollBottomMargin = size.y;
                if (context.escValuesIndex > 0)
                    context.scrollTopMargin = context.escapeValues[0] - 1;

                if (context.escValuesIndex > 1)
                    context.scrollBottomMargin = context.escapeValues[1];

                if (context.scrollTopMargin >= size.y || context.scrollBottomMargin > size.y || context.scrollTopMargin >= (context.scrollBottomMargin - 1))
                {
                    context.scrollTopMargin = 0;
                    context.scrollBottomMargin = size.y;
                }
                SetCursorPos({ 0, 0 });
                break;
        }

        if (prevScrollEnabled) 
            ScrollEnable();

    cleanup:
        context.controlSequence = false;
        context.escape = false;
    }

    void Terminal::ParseEscape(uint8_t c)
    {
        context.escapeOffset++;

        if (context.controlSequence == true)
        {
            ParseControlSequence(c);
            return;
        }

        if (context.csi == true)
        {
            context.csi = false;
            goto is_csi;
        }

        Vector2 cursor;
        cursor = GetCursorPos();

        switch (c)
        {
            case '[':
    is_csi:
                for (size_t i = 0; i < MaxEscValues; i++)
                    context.escapeValues[i] = 0;
                context.escValuesIndex = 0;
                context.rrr = false;
                context.controlSequence = true;
                return;
            case 'c':
                Reinit();
                Clear(true);
                break;
            case 'D':
                if (cursor.y == context.scrollBottomMargin - 1)
                {
                    Scroll();
                    SetCursorPos(cursor);
                }
                else 
                    SetCursorPos({ cursor.x, cursor.y + 1});
                break;
            case 'E':
                if (cursor.y == context.scrollBottomMargin - 1)
                {
                    Scroll();
                    SetCursorPos({ 0, cursor.y });
                }
                else 
                    SetCursorPos({ 0, cursor.y + 1});
                break;
            case 'M':
                if (cursor.y == context.scrollTopMargin)
                {
                    ReverseScroll();
                    SetCursorPos({ 0, cursor.y });
                }
                else 
                    SetCursorPos({ 0, cursor.y - 1 });
                break;
            case '\e':
                RawPutchar(c);
                break;
        }

        context.escape = false;
    }
    
    void Terminal::SetTextFg(size_t index)
    {
        context.textFg = colours[index];
    }

    void Terminal::SetTextBg(size_t index)
    {
        context.textBg = colours[index];
    }

    void Terminal::SetTextFgBright(size_t index)
    {
        context.textFg = brightColours[index];
    }

    void Terminal::SetTextBgBright(size_t index)
    {
        context.textBg = brightColours[index];
    }

    bool Terminal::ScrollDisable()
    {
        const bool prev = context.scrollEnabled;
        context.scrollEnabled = false;
        return prev;
    }

    void Terminal::ScrollEnable()
    {
        context.scrollEnabled = true;
    }

    void Terminal::Scroll()
    {
        for (size_t i = (context.scrollTopMargin + 1) * size.x; i < context.scrollBottomMargin * size.x; i++)
        {
            GTQueueItem* q = map[i];
            GTChar* c = q == nullptr ? &grid[i] : &q->c;
            QueueOp(c, (i - size.x) % size.x, (i - size.x) / size.x);
        }

        GTChar empty { ' ', context.textFg, context.textBg };
        for (size_t i = (context.scrollBottomMargin - 1) * size.x; i < context.scrollBottomMargin * size.x; i++)
            QueueOp(&empty, i % size.x, i / size.x);
    }

    void Terminal::ReverseScroll()
    {
        for (size_t i = (context.scrollBottomMargin - 1) * size.x - 1; ; i--)
        {
            GTQueueItem* q = map[i];
            GTChar* c = q == nullptr ? &grid[i] : &q->c;

            QueueOp(c, (i + size.x) % size.x, (i + size.x) / size.x);
            if (i == context.scrollTopMargin * size.x) 
                break;
        }

        GTChar empty { ' ', context.textFg, context.textBg};
        for (size_t i = context.scrollTopMargin * size.x; i < (context.scrollTopMargin + 1) * size.x; i++)
            QueueOp(&empty, i % size.x, i / size.x);
    }

    void Terminal::SwapPalette()
    {
        const uint32_t tmp = context.textBg;
        context.textBg = context.textFg;
        context.textFg = tmp;
    }

    bool Terminal::Init(const GTStyle& style)
    {
        if (initialized)
            return true;
        
        initialized = true;
        Deinit();

        const GTFont font = { (uintptr_t)FontBegin, { 8, 14 },  1 };
        if (Boot::framebufferRequest.response == nullptr)
            return false;
        auto& fb0 = Boot::framebufferRequest.response->framebuffers[0];
        fb = { (uintptr_t)fb0->address, { fb0->width, fb0->height }, fb0->pitch };
        fbAddr = reinterpret_cast<volatile uint32_t*>(fb.address);

#ifdef NP_INCLUDE_TERMINAL_BG
        background = new GTImage();
        OpenImage(*background, (uintptr_t)BackgroundBegin, (uintptr_t)BackgroundEnd - (uintptr_t)BackgroundBegin);
#else
        background = nullptr;
#endif
        
        context.cursorVisible = true;
        context.scrollEnabled = true;

        sl::memcopy(style.colours, colours, 32);
        sl::memcopy(style.brightColours, brightColours, 32);
        defaultBg = style.background;
        defaultFg = style.foreground & 0xFFFFFF;
        context.textFg = defaultFg;
        context.textBg = 0xFFFFFFFF;
        margin = this->background == nullptr ? 0 : style.margin;

        fontSize.x = font.size.x;
        fontSize.y = font.size.y;

        constexpr size_t vgaFontGlyphs = 256;
        vgaFontBits = reinterpret_cast<const uint8_t*>(font.address);

        fontSize.x += style.fontSpacing;
        vgaFontBoolSize = vgaFontGlyphs * fontSize.y * fontSize.x * sizeof(bool);
        vgaFontBool = new bool[vgaFontBoolSize / sizeof(bool)];

        for (size_t i = 0; i < vgaFontGlyphs; i++)
        {
            const uint8_t* glyph = &vgaFontBits[i * fontSize.y];

            for (size_t y = 0; y < fontSize.y; y++)
            {
                for (size_t x = 0; x < 8; x++)
                {
                    const size_t offset = i * fontSize.y * fontSize.x + y * fontSize.x + x;

                    if ((glyph[y] & (0x80 >> x)))
                        vgaFontBool[offset] = true;
                    else
                        vgaFontBool[offset] = false;
                }

                for (size_t x = 8; x < fontSize.x; x++)
                {
                    const size_t offset = i * fontSize.y * fontSize.x + y * fontSize.x + x;

                    if (i >= 0xC0 && i <= 0xDF)
                        vgaFontBool[offset] = (glyph[y] & 1);
                    else
                        vgaFontBool[offset] = false;
                }
            }
        }

        size.x = (fb.size.x - margin * 2) / fontSize.x;
        size.y = (fb.size.y - margin * 2) / fontSize.y;

        offset.x = margin + ((fb.size.x - margin * 2) % fontSize.x) / 2;
        offset.y = margin + ((fb.size.y - margin * 2) % fontSize.y) / 2;

        gridSize = size.y * size.x * sizeof(GTChar);
        grid = new GTChar[gridSize / sizeof(GTChar)];

        queueSize = size.y * size.x * sizeof(GTQueueItem);
        queue = new GTQueueItem[queueSize / sizeof(GTQueueItem)];
        queueIndex = 0;

        mapSize = size.y * size.x * sizeof(void*);
        map = new GTQueueItem*[mapSize / sizeof(void*)];

        bgCanvasSize = fb.size.x * fb.size.y * sizeof(uint32_t);
        bgCanvas = new uint32_t[bgCanvasSize / sizeof(uint32_t)];

        GenerateCanvas();
        Clear(true);
        Flush();
        Reinit();
        return true;
    }

    void Terminal::Deinit()
    {
        if (!initialized)
            return;
        
        if (background)
        {
            CloseImage(*background);
            delete background;
        }

        vgaFontBits = nullptr;
        delete[] vgaFontBool;
        delete[] grid;
        delete[] queue;
        delete[] map;
        delete[] bgCanvas;
    }

    void Terminal::Reinit()
    {
        if (!initialized)
            return;
        
        context.escapeOffset = 0;
        context.controlSequence = false;
        context.csi = false;
        context.escape = false;
        context.rrr = false;
        context.discardNext = false;
        context.bold = false;
        context.escValuesIndex = 0;
        context.currentPrimary = -1;
        context.scrollTopMargin = 0;
        context.scrollBottomMargin = size.y;
        autoflush = true;
    }

    void Terminal::Write(const char* str, size_t length)
    {
        if (!initialized)
            return;

        for (size_t i = 0; i < length; i++)
            Putchar(str[i]);
        
        if (autoflush)
            Flush();
    }

    void Terminal::Clear(bool move)
    {
        if (!initialized) 
            return;

        GTChar empty { ' ', context.textFg, context.textBg };
        for (size_t i = 0; i < size.y * size.x; i++)
            QueueOp(&empty, i % size.x, i / size.x);

        if (move)
            context.cursor.x = context.cursor.y = 0;
    }

    void Terminal::Flush()
    {
        if (!initialized)
            return;

        if (context.cursorVisible) 
            DrawCursor();

        for (size_t i = 0; i < queueIndex; i++)
        {
            GTQueueItem* q = &queue[i];
            const size_t offset = q->pos.y * size.x + q->pos.x;
            if (map[offset] == nullptr) 
                continue;

            GTChar* old = &grid[offset];
            if (q->c.bg == old->bg && q->c.fg == old->fg)
                PlotCharFast(old, &q->c, q->pos.x, q->pos.y);
            else
                PlotChar(&q->c, q->pos.x, q->pos.y);

            grid[offset] = q->c;
            map[offset] = nullptr;
        }

        if ((oldCursor.x != context.cursor.x || oldCursor.y != context.cursor.y) || context.cursorVisible == false)
            PlotChar(&grid[oldCursor.x + oldCursor.y * size.x], oldCursor.x, oldCursor.y);

        oldCursor.x = context.cursor.x;
        oldCursor.y = context.cursor.y;
        queueIndex = 0;
    }
    
    void Terminal::EnableCursor()
    {
        if (!initialized)
            return;

        context.cursorVisible = true;
    }

    bool Terminal::DisableCursor()
    {
        if (!initialized)
            return false;
        
        const bool prev = context.cursorVisible;
        context.cursorVisible = false;
        return prev;
    }

    void Terminal::SetCursorPos(Vector2 pos)
    {
        if (!initialized)
            return;
        
        if (pos.x >= size.x)
            pos.x = (int)pos.x < 0 ? 0 : size.x - 1;
        if (pos.y >= size.y)
            pos.y = (int)pos.y < 0 ? 0 : size.y - 1;
        
        context.cursor.x = pos.x;
        context.cursor.y = pos.y;
    }

    Vector2 Terminal::GetCursorPos()
    {
        return initialized ? context.cursor : Vector2{ 0, 0 };
    }
}
