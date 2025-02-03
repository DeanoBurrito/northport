#include <Terminal.h>
#include <Maths.h>
#include <CppUtils.h>
#include <NativePtr.h>

/* This code has been rewritten a few times, but it originally stems from
 * the limine bootloader's gterm (now moved into a separate project called flanterm).
 * The license text from when I copied this code from limine is as follows:
 * 
 * BSD 2-Clause License
 * 
 * Copyright (c) 2019, 2020, 2021, 2022, mintsuki and contributors
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
namespace sl
{
    asm("FontBegin: \n .incbin \"../../misc/TerminalFont\" \n");
    extern uint8_t FontBegin[] asm("FontBegin");
    constexpr Vector2u FontSize = { 8, 14 };
    constexpr size_t FontGlyphCount = 256;

    void Terminal::QueueOp(Char c, Vector2u pos)
    {
        auto CompareChar = [](const Char& a, const Char& b)
        {
            return !(a.value != b.value || a.bg != b.bg || a.fg != b.fg);
        };

        if (pos.x >= size.x || pos.y >= size.y)
            return;

        const size_t index = pos.y * size.x + pos.x;

        QueuedOp* op = queuedOpsMap[index];
        if (op == nullptr)
        {
            if (CompareChar(currChars[index], c))
                return;
            op = &queuedOpStore[queuedOpHead++];
            op->pos = pos;
            op->c = c;
            queuedOpsMap[index] = op;
        }
        op->c = c;
    }

    void Terminal::DrawChar(Vector2u where, Char old, Char next)
    {
        if (where.x >= size.x || where.y >= size.y)
            return;

        const bool canSkipRender = old.value != 0 && (old.bg == next.bg) && (old.fg == next.fg) && false;
        const size_t strideInPixels = fbStride / sizeof(uint32_t);

        where.x = margin.x + where.x * fontSize.x;
        where.y = margin.y + where.y * fontSize.y;
        const bool* nextGlyph = &fontStore[next.value * fontSize.x * fontSize.y];
        const bool* oldGlyph = &fontStore[old.value * fontSize.x * fontSize.y];

        for (size_t y = 0; y < fontSize.y; y++)
        {
            volatile uint32_t* fbLine = fbBase + where.x + (where.y + y) * strideInPixels;
            //const uint32_t* canvasLine = bgCanvas + where.x + (where.y + y) * fbSize.x;
            const volatile uint32_t* canvasLine = fbLine;
            const size_t glyphBaseIndex = y * fontSize.x;

            for (size_t x = 0; x < fontSize.x; x++)
            {
                const bool nextPixel = nextGlyph[glyphBaseIndex + x];
                if (canSkipRender && nextPixel == oldGlyph[glyphBaseIndex + x])
                    continue;

                if (nextPixel)
                    fbLine[x] = (next.fg == 0xFFFF'FFFF ? canvasLine[x] : next.fg);
                else
                    fbLine[x] = (next.bg == 0xFFFF'FFFF ? canvasLine[x] : next.bg);
            }
        }
    }

    void Terminal::DrawCursor()
    {
        const size_t index = cursorPos.y * size.x + cursorPos.x;
        Char obscured = currChars[index];

        QueuedOp* op = queuedOpsMap[index];
        if (op != nullptr)
            obscured = op->c;
        Swap(obscured.fg, obscured.bg);
        DrawChar(cursorPos, {}, obscured);

        if (op != nullptr)
        {
            currChars[index] = obscured;
            queuedOpsMap[index] = nullptr;
        }
    }

    void Terminal::QueueScroll()
    {
        for (size_t y = 1; y < size.y; y++)
        {
            for (size_t x = 0; x < size.x; x++)
            {
                const size_t index = x + y * size.x;
                const QueuedOp* queued = queuedOpsMap[index];

                if (queued == nullptr)
                    QueueOp(currChars[index], { x, y - 1 });
                else
                    QueueOp(queued->c, { x, y - 1 });
            }
        }

        Char blankChar { currFg, currBg, ' ' };
        for (size_t x = 0; x < size.x; x++)
            QueueOp(blankChar, { x, size.y - 1 });
    }

    void Terminal::QueueChar(uint32_t character)
    {
        const Char termChar = { currFg, currBg, character };
        QueueOp(termChar, cursorPos);
        cursorPos.x++;

        if (cursorPos.x == size.x && cursorPos.y < size.y - 1)
        {
            cursorPos.x = 0;
            cursorPos.y++;
        }

        if (cursorPos.y == size.y)
        {
            cursorPos.y--;
            QueueScroll();
        }
    }

    void Terminal::HandleSgr()
    {
        if (parseState.escArgIndex == 0)
        {
            currFg = defaultFg;
            currBg = defaultBg;
            return;
        }

        for (size_t i = 0; i < parseState.escArgIndex; i++)
        {
            const char arg = parseState.escArgs[i];

            switch (arg)
            {
            case 0:
                currFg = defaultFg;
                currBg = defaultBg;
                continue;
            case 7:
                {
                    const uint32_t temp = currFg;
                    currFg = currBg;
                    currBg = temp;
                    continue;
                }
            case 39:
                currFg = defaultFg;
                continue;
            case 49:
                currBg = defaultBg;
                continue;
            default:
                if (arg >= 30 && arg <= 37)
                    currFg = colours[arg - 30];
                else if (arg >= 40 && arg <= 47)
                    currBg = colours[arg - 40];
                else if (arg >= 90 && arg <= 97)
                    currFg = brightColours[arg - 90];
                else if (arg >= 100 && arg <= 107)
                    currFg = brightColours[arg - 100];
                continue; //TODO: truecolour support?
            }
        }
    }

    void Terminal::HandleEraseScreen()
    {
    }

    void Terminal::ParseControlSequence(const char c)
    {
        if (c >= '0' && c <= '9')
        {
            if (parseState.escArgIndex == EscapeArgsCount)
                return;

            parseState.inEscArg = true;
            auto& store = parseState.escArgs[parseState.escArgIndex];
            store = store * 10 + (c - '0');
            return;
        }

        if (parseState.inEscArg)
        {
            parseState.escArgIndex++;
            parseState.inEscArg = false;
            if (c == ';')
                return;
        }
        else if (c == ';')
        {
            if (parseState.escArgIndex == EscapeArgsCount)
                return;
            parseState.escArgs[parseState.escArgIndex++] = 0;
            return;
        }

        for (size_t i = parseState.escArgIndex; i < EscapeArgsCount; i++)
        {
            const size_t defaultValue = (c == 'J' || c == 'K' || c == 'q') ? 0 : 1;
            parseState.escArgs[i] = defaultValue;
        }

        switch (c)
        {
        case 'F': //move cursorPos up, start of line
            cursorPos.x = 0;
        case 'A': //move cursorPos up
            sl::MinInPlace<unsigned>(parseState.escArgs[0], cursorPos.y);
            SetCursorPos({ cursorPos.x, cursorPos.y - parseState.escArgs[0] });
            break;

        case 'E': //move cursorPos down, start of line
            cursorPos.x = 0;
        case 'e':
        case 'B': //move cursorPos down
            sl::MinInPlace<unsigned>(parseState.escArgs[0], (size.y - 1) - cursorPos.y);
            SetCursorPos({ cursorPos.x, cursorPos.y + parseState.escArgs[0] });
            break;

        case 'a':
        case 'C': //move cursorPos right
            sl::MinInPlace<unsigned>(parseState.escArgs[0], (size.x - 1) - cursorPos.x);
            SetCursorPos({ cursorPos.x + parseState.escArgs[0], cursorPos.y });
            break;

        case 'D': //move cursor left
            sl::MinInPlace<unsigned>(parseState.escArgs[0], cursorPos.x);
            SetCursorPos({ cursorPos.x - parseState.escArgs[0], cursorPos.y });
            break;

        case 'G': //set cursor column
            parseState.escArgs[0] -= 1;
            sl::MinInPlace<unsigned>(parseState.escArgs[0], size.y - 1);
            SetCursorPos({ parseState.escArgs[0], cursorPos.y });
            break;

        case 'H': //set cursor position
        case 'f':
            parseState.escArgs[0] -= 1;
            parseState.escArgs[1] -= 1;
            sl::MinInPlace<unsigned>(parseState.escArgs[0], size.y - 1);
            sl::MinInPlace<unsigned>(parseState.escArgs[1], size.x - 1);
            SetCursorPos({ parseState.escArgs[1], parseState.escArgs[0] });
            break;

        case 'J': //erase screen
            HandleEraseScreen();
            break;
            
        case 'm': //manipulate colours
            HandleSgr();
            break;

        case 'K': //erase line
        {
            const auto oldCursor = cursorPos;
            const size_t begin = (parseState.escArgs[0] == 0) ? cursorPos.x : 0;
            const size_t end = (parseState.escArgs[0] == 2) ? size.x : cursorPos.x;

            cursorPos.x = begin;
            for (size_t i = begin; i < end; i++)
                QueueChar(' ');
            cursorPos = oldCursor;
        }

        }

        parseState.inControlSeq = false;
        parseState.inEscape = false;
    }

    void Terminal::ProcessString(StringSpan str)
    {
        for (size_t i = 0; i < str.Size(); i++)
        {
            if (parseState.inEscape)
            {
                if (parseState.inControlSeq)
                {
                    ParseControlSequence(str[i]);
                    continue;
                }

                if (str[i] == '[')
                {
                    parseState.escArgIndex = 0;
                    parseState.inControlSeq = true;
                    parseState.inEscArg = false;

                    for (size_t i = 0; i < EscapeArgsCount; i++)
                        parseState.escArgs[i] = 0;
                }
                else
                    parseState.inEscape = false;
                continue;
            }

            switch (str[i])
            {
            case '\e':
                parseState.inEscape = true;
                continue;
            case '\r':
                SetCursorPos({ 0, cursorPos.y });
                continue;
            case '\n':
                if (cursorPos.y == size.y - 1)
                    QueueScroll();
                else
                    SetCursorPos({ 0, cursorPos.y + 1 });
                continue;
            case '\t':
                SetCursorPos({ sl::Min(size.x - 1, (cursorPos.x / tabSize + 1) * tabSize), cursorPos.y });
                continue;
            case '\b':
                SetCursorPos({ cursorPos.x - 1, cursorPos.y });
                continue;
            default:
                QueueChar(str[i]);
                continue;
            }
        }
    }

    bool Terminal::Init(const TerminalConfig& config)
    {
        if (initialized)
            return true;

        if (config.fbBase == nullptr || config.fbSize.x == 0 || config.fbSize.y == 0
            || config.fbStride < config.fbSize.x * 4)
            return false;
        if (config.Alloc == nullptr)
            return false;

        tabSize = sl::Max<size_t>(config.tabSize, 1);
        const size_t validMargin = sl::Min<size_t>(config.margin, config.fbSize.x / 2);
        const size_t validFontSpacing = sl::Clamp(config.fontSpacing, 0ul, FontSize.x);

        fbBase = static_cast<uint32_t*>(config.fbBase);
        fbStride = config.fbStride;

        cursorPos = prevCursorPos = { 0, 0 };
        margin = { validMargin, validMargin };
        fontSize = FontSize;
        cursorVisible = false;

        for (size_t i = 0; i < TerminalColourCount; i++)
        {
            colours[i] = config.colours[i]; //TODO: transparency mask
            brightColours[i] = config.brightColours[i];
        }
        currBg = config.background;
        currFg = config.foreground;
        defaultBg = config.background;
        defaultFg = config.foreground;

        fontSize.x += validFontSpacing;
        const size_t fontStoreSize = FontGlyphCount * fontSize.x * fontSize.y * sizeof(bool);
        void* maybeFontDataBuffer = config.Alloc(fontStoreSize);
        if (maybeFontDataBuffer == nullptr)
            return false;
        fontStore = { (bool*)maybeFontDataBuffer, fontStoreSize / sizeof(bool) };

        const uint8_t* fontData = reinterpret_cast<const uint8_t*>(FontBegin);
        for (size_t i = 0; i < FontGlyphCount; i++)
        {
            const uint8_t* glyph = &fontData[i * fontSize.y];

            for (size_t y = 0; y < fontSize.y; y++)
            {
                const size_t offset = i * fontSize.y * fontSize.x + y * fontSize.x;
                for (size_t x = 0; x < 8; x++)
                    fontStore[offset + x] = glyph[y] & (0x80 >> x);

                for (size_t x = 8; x < fontSize.x; x++)
                    fontStore[offset + x] = (i >= 0xC0) && (i <= 0xDF) && (glyph[y] & 1);
            }
        }

        size.x = (config.fbSize.x - margin.x * 2) / fontSize.x;
        size.y = (config.fbSize.y - margin.y * 2) / fontSize.y;

        const size_t charsCount = size.x * size.y;
        const size_t currCharsSize = charsCount * sizeof(Char);
        currChars = static_cast<Char*>(config.Alloc(currCharsSize));
        if (currChars == nullptr)
            return false;

        const size_t queueSize = charsCount * sizeof(QueuedOp);
        queuedOpStore = static_cast<QueuedOp*>(config.Alloc(queueSize));
        if (queuedOpStore == nullptr)
            return false;
        queuedOpHead = 0;

        const size_t mapSize = charsCount * sizeof(void*);
        queuedOpsMap = static_cast<QueuedOp**>(config.Alloc(mapSize));
        if (queuedOpsMap == nullptr)
            return false;
        for (size_t i = 0; i < charsCount; i++)
            queuedOpsMap[i] = nullptr;

        const Char clearChar = { currFg, currBg, ' ' };
        for (size_t y = 0; y < size.y; y++)
        {
            for (size_t x = 0; x < size.x; x++)
                DrawChar({ x, y }, {}, clearChar);
        }

        parseState.inEscape = false;
        parseState.inControlSeq = false;
        parseState.inEscArg = false;

        //TODO: background canvas
        initialized = true;
        return true;
    }

    void Terminal::Deinit(void (*Free)(void* ptr, size_t size))
    {
        if (!initialized)
            return;
        if (Free == nullptr)
            return;

        Free(fontStore.Begin(), FontGlyphCount * fontSize.x * fontSize.y * sizeof(bool));
        Free(currChars, size.x * size.y * sizeof(Char));
        Free(queuedOpStore, size.x * size.y * sizeof(QueuedOp));
        Free(queuedOpsMap, size.x * size.y * sizeof(void*));
        //TODO: background canvas
    }

    void Terminal::Write(StringSpan span, bool flush)
    {
        if (!initialized)
            return;

        ProcessString(span);
        if (flush)
            Flush();
    }

    void Terminal::Flush()
    {
        if (!initialized)
            return;

        if (cursorVisible)
            DrawCursor();

        for (size_t i = 0; i < queuedOpHead; i++)
        {
            const QueuedOp* op = &queuedOpStore[i];
            const size_t index = op->pos.x + op->pos.y * size.x;
            if (queuedOpsMap[index] == nullptr)
                continue;

            DrawChar(op->pos, currChars[index], op->c);
            currChars[index] = op->c;
            queuedOpsMap[index] = nullptr;
        }

        if (!cursorVisible || prevCursorPos.x != cursorPos.x || prevCursorPos.y != cursorPos.y)
            DrawChar(prevCursorPos, {}, currChars[prevCursorPos.x + prevCursorPos.y * size.x]);

        prevCursorPos = cursorPos;
        queuedOpHead = 0;
    }

    void Terminal::EnableCursor(bool yes)
    {
        if (!initialized)
            return;
        cursorVisible = yes;
    }

    void Terminal::SetCursorPos(Vector2u where)
    {
        if (!initialized)
            return;

        cursorPos.x = Clamp<size_t>(where.x, 0, size.x - 1);
        cursorPos.y = Clamp<size_t>(where.y, 0, size.y - 1);
    }

    Vector2u Terminal::GetCursorPos() const
    {
        if (!initialized)
            return {};
        return cursorPos;
    }
}
