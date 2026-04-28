#include <private/Video.hpp>
#include <lib/Maths.hpp>
#include <lib/Mmio.hpp>

namespace Npk
{
    constexpr size_t TabSize = 4;
    constexpr size_t GlyphCount = 256;
    constexpr decltype(TextRenderer::fgIndex) FgBgIndexSpecial = -1;

    TextRendererConfig* CreateTextRendererConfig(
        sl::Span<const uint8_t> fontData, sl::Vector2u fontSize, 
        sl::Span<const Colour> dim, sl::Span<const Colour> bright, 
        Colour foreground, Colour background)
    {
        if (fontData.Size() < (fontSize.x * fontSize.y * GlyphCount) / 8)
            return nullptr;

        if (dim.Size() != 8)
            return nullptr;
        if (bright.Size() != 8)
            return nullptr;

        void* ptr = PoolAllocWired(sizeof(TextRendererConfig), VideoTag);
        if (ptr == nullptr)
            return nullptr;
        auto* config = new(ptr) TextRendererConfig {};

        const size_t fontBitsSize = fontSize.x * fontSize.y * GlyphCount;
        ptr = PoolAllocWired(fontBitsSize, VideoTag);
        if (ptr == nullptr)
        {
            PoolFreeWired(config, sizeof(TextRendererConfig), VideoTag);
            return nullptr;
        }
        config->font = { static_cast<bool*>(ptr), fontBitsSize };

        for (size_t i = 0; i < GlyphCount; i++)
        {
            const uint8_t* glyph = &fontData[i * fontSize.y];

            for (size_t y = 0; y < fontSize.y; y++)
            {
                const size_t offset = i * fontSize.y * fontSize.x + y * 
                    fontSize.x;

                for (size_t x = 0; x < 8; x++)
                    config->font[offset + x] = glyph[y] & (0x80 >> x);

                for (size_t x = 8; x < fontSize.x; x++)
                {
                    config->font[offset + x] = 
                        (i >= 0xC0) && (i <= 0xDF) && (glyph[y] & 1);
                }
            }
        }

        config->fontSize = fontSize;
        for (size_t i = 0; i < ColourCount; i++)
        {
            config->colours[i] = dim[i];
            config->brightColours[i] = bright[i];
        }
        config->background = background;
        config->foreground = foreground;

        return config;
    }

    TextRenderer* CreateTextRenderer(TextRendererConfig* conf,
        sl::Vector2u size)
    {
        NPK_CHECK(conf != nullptr, nullptr);
        NPK_CHECK(size.x > 40 && size.y > 20, nullptr);

        const size_t cellCount = size.x * size.y;
        void* ptr = PoolAllocWired(sizeof(TextRenderer), VideoTag);
        NPK_CHECK(ptr != nullptr, nullptr);

        auto* engine = new(ptr) TextRenderer {};
        NPK_ASSERT(ResetMutex(&engine->mutex, 1));
        engine->config = conf;

        ptr = PoolAllocWired(cellCount * sizeof(PendingWrite), VideoTag);
        if (ptr == nullptr)
        {
            PoolFreeWired(engine, sizeof(TextRenderer), VideoTag);
            return nullptr;
        }
        engine->pending = { static_cast<PendingWrite*>(ptr), cellCount };

        ptr = PoolAllocWired(cellCount * sizeof(void*), VideoTag);
        if (ptr == nullptr)
        {
            PoolFreeWired(engine->pending.Begin(), engine->pending.SizeBytes(),
                VideoTag);
            PoolFreeWired(engine, sizeof(TextRenderer), VideoTag);
            return nullptr;
        }
        engine->pendingMap = { static_cast<PendingWrite**>(ptr), cellCount };

        ptr = PoolAllocWired(cellCount * sizeof(Cell), VideoTag);
        if (ptr == nullptr)
        {
            PoolFreeWired(engine->pendingMap.Begin(), 
                engine->pendingMap.SizeBytes(), VideoTag);
            PoolFreeWired(engine->pending.Begin(), engine->pending.SizeBytes(),
                VideoTag);
            PoolFreeWired(engine, sizeof(TextRenderer), VideoTag);
            return nullptr;
        }
        engine->cells = { static_cast<Cell*>(ptr), cellCount };

        for (size_t i = 0; i < engine->cells.Size(); i++)
        {
            auto& cell = engine->cells[i];

            cell.fgIndex = FgBgIndexSpecial;
            cell.bgIndex = FgBgIndexSpecial;
            cell.data = ' ';
        }

        engine->size = size;
        engine->fgIndex = FgBgIndexSpecial;
        engine->bgIndex = FgBgIndexSpecial;

        Log("Created text renderer engine: %zu x %zu cells, %p", LogLevel::Info,
            size.x, size.y, engine);

        return engine;
    }

    static Pixel ColourToPixel(SimpleFramebuffer* fb, Colour c)
    {
        auto FormatChannel = [](size_t value, size_t bits, size_t offset) 
            -> Pixel
        {
            const auto srcLimit = 1 << (sizeof(c.red) * 8);
            const auto destLimit = 1 << bits;

            value = (value * destLimit) / srcLimit;
            value = value << offset;

            return value;
        };

        Pixel px {};
        px |= FormatChannel(c.red, fb->redBits, fb->redShift);
        px |= FormatChannel(c.green, fb->greenBits, fb->greenShift);
        px |= FormatChannel(c.blue, fb->blueBits, fb->blueShift);

        return px;
    }

    //NOTE: this function assumes `target` is aligned to `sizeof(pixel)`.
    static void WritePixel(Pixel value, size_t pixelBytes, uintptr_t target)
    {
        switch (pixelBytes)
        {
        case 8:
            sl::MmioWrite64(target, value);
            break;

        case 4:
            sl::MmioWrite32(target, value);
            break;

        case 2:
            sl::MmioWrite16(target, value);
            break;

        default:
            for (size_t i = 0; i < pixelBytes; i++)
                sl::MmioWrite8(target + i, value >> (i * 8));
            break;
        }
    }

    TextAdaptor* CreateTextAdaptor(TextRenderer* renderer, 
        SimpleFramebuffer* fb, sl::Vector2u margin)
    {
        NPK_CHECK(renderer != nullptr, nullptr);
        NPK_CHECK(fb != nullptr, nullptr);

        void* ptr = PoolAllocWired(sizeof(TextAdaptor), VideoTag);
        NPK_CHECK(ptr != nullptr, nullptr);

        auto* adaptor = new(ptr) TextAdaptor {};
        adaptor->renderer = renderer;
        adaptor->framebuffer = fb;
        adaptor->margin = margin;

        auto& conf = renderer->config;
        const auto fontSize = conf->fontSize;
        adaptor->scale.x = fb->width / (renderer->size.x * fontSize.x);
        adaptor->scale.y = fb->height / (renderer->size.y * fontSize.y);
        adaptor->scale.x = sl::Min(adaptor->scale.x, adaptor->scale.y);

        if (adaptor->scale.x < 1)
            adaptor->scale.x = 1;
        adaptor->scale.y = adaptor->scale.x;

        adaptor->offset.x = fb->width;
        adaptor->offset.x -= renderer->size.x * fontSize.x * adaptor->scale.x;
        adaptor->offset.x /= 2;

        adaptor->offset.y = fb->height;
        adaptor->offset.y -= renderer->size.y * fontSize.y * adaptor->scale.y;
        adaptor->offset.y /= 2;

        adaptor->background = ColourToPixel(fb, conf->background);
        adaptor->foreground = ColourToPixel(fb, conf->foreground);
        for (size_t i = 0; i < ColourCount; i++)
        {
            adaptor->dimCols[i] = ColourToPixel(fb, conf->colours[i]);
            adaptor->brightCols[i] = ColourToPixel(fb, conf->brightColours[i]);
        }

        if (!AcquireMutex(&renderer->mutex, sl::NoTimeout))
        {
            PoolFreeWired(ptr, sizeof(TextAdaptor), VideoTag);

            return nullptr;
        }
        renderer->adaptors.PushBack(adaptor);
        ReleaseMutex(&renderer->mutex);

        //TODO: allow user to specify a different colour for the margin area.
        const size_t bytesPerPixel = fb->bpp / 8;
        for (size_t y = 0; y < fb->height; y++)
        {
            for (size_t x = 0; x < fb->width; x++)
            {
                auto target = fb->vbase + y * fb->pitch + x * bytesPerPixel;
                WritePixel(adaptor->background, bytesPerPixel, target);
            }
        }

        Log("Created text adaptor: %zu x %zu pixels, scale=%zu,%zu, fb=0x%tx",
            LogLevel::Info, fb->width, fb->height, adaptor->scale.x,
            adaptor->scale.y, fb->vbase);

        FullRefreshTextAdaptor(renderer, adaptor);

        return adaptor;
    }

    static void RenderCell(TextAdaptor* adaptor, sl::Vector2u where, Cell cell)
    {
        auto& fb = adaptor->framebuffer;
        auto& config = adaptor->renderer->config;
        auto& font = config->font;
        auto& fontSize = config->fontSize;
        const auto scale = adaptor->scale;

        where.x = adaptor->offset.x + adaptor->margin.x + where.x * fontSize.x
            * scale.x;
        where.y = adaptor->offset.y + adaptor->margin.y + where.y * fontSize.y
            * scale.y;

        if (where.x + fontSize.x * scale.x >= fb->width - adaptor->margin.x || 
            where.y + fontSize.y * scale.y >= fb->height - adaptor->margin.y)
            return;

        const Pixel fg = [=]() 
        {
            if (cell.fgIndex == FgBgIndexSpecial)
                return adaptor->foreground;
            if (cell.fgIndex >= ColourCount)
                return adaptor->brightCols[cell.fgIndex - ColourCount];
            return adaptor->dimCols[cell.fgIndex];
        }();

        const Pixel bg = [=]()
        {
            if (cell.bgIndex == FgBgIndexSpecial)
                return adaptor->background;
            if (cell.bgIndex >= ColourCount)
                return adaptor->brightCols[cell.bgIndex - ColourCount];
            return adaptor->dimCols[cell.bgIndex];
        }();

        const size_t bytesPerPixel = fb->bpp / 8;
        const bool* nextGlyph = &font[(uint8_t)cell.data * 
            fontSize.x * fontSize.y];

        for (size_t y = 0; y < fontSize.y; y++)
        {
            const size_t glyphRow = y * fontSize.x;

            for (size_t sy = 0; sy < scale.y; sy++)
            {
                auto line = (where.y + y * scale.y + sy) * fb->pitch;
                line += fb->vbase;

                for (size_t x = 0; x < fontSize.x; x++)
                {
                    auto px = nextGlyph[glyphRow + x] ? fg : bg;
                    uintptr_t target = line + (where.x + x + scale.x) *
                        bytesPerPixel;
                    for (size_t sx = 0; sx < scale.x; sx++)
                    {
                        WritePixel(px, bytesPerPixel, 
                            target + sx * bytesPerPixel);
                    }
                }
            }
        }
    }

    //NOTE: expects engine->mutex to be held
    static void QueueOp(TextRenderer* engine, Cell c, sl::Vector2u where)
    {
        if (where.x >= engine->size.x || where.y >= engine->size.y)
            return;

        const size_t index = where.x + where.y * engine->size.x;
        auto* op = engine->pendingMap[index];

        if (op == nullptr)
        {
            if (engine->cells[index].data == c.data &&
                engine->cells[index].bgIndex == c.bgIndex &&
                engine->cells[index].fgIndex == c.fgIndex)
                return;

            op = &engine->pending[engine->pendingHead++];
            op->pos = where;
            op->cell = c;

            engine->pendingMap[index] = op;
        }

        op->cell = c;
    }

    //NOTE: expects engine->mutex to be held
    static void QueueScroll(TextRenderer* engine)
    {
        for (size_t y = 1; y < engine->size.y; y++)
        {
            for (size_t x = 0; x < engine->size.x; x++)
            {
                const size_t index = x + y * engine->size.x;
                const auto* op = engine->pendingMap[index];

                Cell value = engine->cells[index];
                if (op != nullptr)
                    value = op->cell;
                QueueOp(engine, value, { x, y - 1 });
            }
        }

        Cell blank { engine->fgIndex, engine->bgIndex, ' ' };
        for (size_t x = 0; x < engine->size.x; x++)
            QueueOp(engine, blank, { x, engine->size.y - 1 });
    }

    //NOTE: expects engine->mutex to be held
    static void QueueChar(TextRenderer* engine, const char c)
    {
        const Cell cell { engine->fgIndex, engine->bgIndex, c };
        QueueOp(engine, cell, engine->cursor);
        
        engine->cursor.x++;
        if (engine->cursor.x == engine->size.x 
            && engine->cursor.y < engine->size.y - 1)
        {
            engine->cursor.x = 0;
            engine->cursor.y++;
        }

        if (engine->cursor.y == engine->size.y)
        {
            engine->cursor.y--;
            QueueScroll(engine);
        }
    }

    //NOTE: expects engine->mutex to be held
    static void ParseSgr(TextRenderer* engine)
    {
        auto& parser = engine->parser;

        if (parser.argIndex == 0)
        {
            engine->fgIndex = FgBgIndexSpecial;
            engine->bgIndex = FgBgIndexSpecial;
            return;
        }

        for (size_t i = 0; i < parser.argIndex; i++)
        {
            const auto arg = parser.args[i];

            switch (arg)
            {
            case 0:
                engine->fgIndex = FgBgIndexSpecial;
                engine->bgIndex = FgBgIndexSpecial;
                break;

            case 7:
                sl::Swap(engine->fgIndex, engine->bgIndex);
                break;

            case 39:
                engine->fgIndex = FgBgIndexSpecial;
                break;

            case 49:
                engine->bgIndex = FgBgIndexSpecial;
                break;

            default:
                if (arg >= 30 && arg <= 37)
                    engine->fgIndex = arg - 30;
                else if (arg >= 40 && arg <= 47)
                    engine->bgIndex = arg - 40;
                else if (arg >= 90 && arg <= 97)
                    engine->fgIndex = arg - 90 + ColourCount;
                else if (arg >= 100 && arg <= 107)
                    engine->bgIndex = arg - 100 + ColourCount;
                break;
            }
        }
    }

    //NOTE: expects engine->mutex to be held
    static void ParseControlSequence(TextRenderer* engine, const char c)
    {
        auto& parser = engine->parser;

        if (c >= '0' && c <= '9')
        {
            if (parser.argIndex == CsiArgCount)
                return;

            parser.inEscArg = true;
            auto& store = parser.args[parser.argIndex];
            store = store * 10 + (c - '0');
            return;
        }

        if (parser.inEscArg)
        {
            parser.argIndex++;
            parser.inEscArg = false;
            if (c == ';')
                return;
        }
        else if (c == ';')
        {
            if (parser.argIndex == CsiArgCount)
                return;
            
            parser.args[parser.argIndex++] = 0;
            return;
        }

        for (size_t i = parser.argIndex; i < CsiArgCount; i++)
        {
            if (c == 'J' || c == 'K' || c == 'q')
                parser.args[i] = 0;
            else
                parser.args[i] = 1;
        }

        switch (c)
        {
        case 'F':
            engine->cursor.x = 0;
            [[fallthrough]];
        case 'A':
            if (parser.args[0] > engine->cursor.y)
                engine->cursor.y = 0;
            else
                engine->cursor.y -= parser.args[0];
            break;

        case 'E':
            engine->cursor.x = 0;
            [[fallthrough]];
        case 'e':
            [[fallthrough]];
        case 'B':
            engine->cursor.y += parser.args[0];
            sl::MinInPlace(engine->cursor.y, engine->size.y - 1);
            break;

        case 'D':
            if (parser.args[0] > engine->cursor.x)
                engine->cursor.x = 0;
            else
                engine->cursor -= parser.args[0];
            break;

        case 'a':
            [[fallthrough]];
        case 'C':
            engine->cursor.x += parser.args[0];
            sl::MinInPlace(engine->cursor.x, engine->size.x - 1);
            break;

        case 'G':
            parser.args[0] -= 1;
            engine->cursor.x = sl::Min<decltype(engine->cursor.x)>(
                parser.args[0], engine->size.x - 1);
            break;

        case 'H':
            [[fallthrough]];
        case 'f':
            parser.args[0] -= 1;
            parser.args[1] -= 1;
            engine->cursor.x = parser.args[1];
            engine->cursor.y = parser.args[0];
            sl::MinInPlace(engine->cursor.x, engine->size.x - 1);
            sl::MinInPlace(engine->cursor.y, engine->size.y - 1);
            break;

        case 'm':
            ParseSgr(engine);
            break;

        case 'K':
            {
                const auto prevCursor = engine->cursor;
                const size_t begin = parser.args[0] == 0 ? engine->cursor.x : 0;
                const size_t end = parser.args[0] == 2 ? engine->size.x : 
                    engine->cursor.x;

                engine->cursor.x = begin;
                for (size_t i = begin; i < end; i++)
                    QueueChar(engine, ' ');
                engine->cursor = prevCursor;
            }
            break;
        }

        parser.inEscape = false;
        parser.inControlSeq = false;
    }

    //NOTE: expects engine->mutex to be held
    static void FlushTextRendererLocked(TextRenderer* engine)
    {
        for (size_t i = 0; i < engine->pendingHead; i++)
        {
            const auto op = engine->pending[i];
            const size_t index = op.pos.x + op.pos.y * engine->size.x;

            if (engine->pendingMap[index] == nullptr)
                continue;

            for (auto it = engine->adaptors.Begin(); 
                it != engine->adaptors.End(); ++it)
            {
                RenderCell(&*it, op.pos, op.cell);
            }

            engine->cells[index] = op.cell;
            engine->pendingMap[index] = nullptr;
        }

        engine->pendingHead = 0;
    }

    //NOTE: expects engine->mutex to be held
    static void WriteChar(TextRenderer* engine, const char c)
    {
        auto& parser = engine->parser;

        if (parser.inEscape)
        {
            if (parser.inControlSeq)
                ParseControlSequence(engine, c);
            else if (c == '[')
            {
                parser.argIndex = 0;
                parser.inControlSeq = true;
                parser.inEscArg = false;

                for (size_t i = 0; i < CsiArgCount; i++)
                    parser.args[i] = 0;
            }
            else
                parser.inEscape = false;

            return;
        }

        switch (c)
        {
        case '\e':
            parser.inEscape = true;
            break;

        case '\r':
            engine->cursor.x = 0;
            break;

        case '\n':
            if (engine->cursor.y == engine->size.y - 1)
            {
                engine->cursor.x = 0;
                QueueScroll(engine);
            }
            else
            {
                engine->cursor.x = 0;
                engine->cursor.y++;
            }
            FlushTextRendererLocked(engine);
            break;

        case '\t':
            engine->cursor.x = sl::Min(engine->size.x - 1, 
                (engine->cursor.x / TabSize + 1) * TabSize);
            break;

        case '\b':
            if (engine->cursor.x > 0)
                engine->cursor.x--;
            break;

        default:
            QueueChar(engine, c);
            break;
        }
    }

    void WriteText(TextRenderer* engine, sl::StringSpan text)
    {
        NPK_ASSERT(engine != nullptr);

        NPK_ASSERT(AcquireMutex(&engine->mutex, sl::NoTimeout));
        for (size_t i = 0; i < text.Size(); i++)
            WriteChar(engine, text[i]);
        ReleaseMutex(&engine->mutex);
    }

    void FlushTextRenderer(TextRenderer* engine)
    {
        NPK_ASSERT(engine != nullptr);

        if (!engine->panicMode)
            NPK_ASSERT(AcquireMutex(&engine->mutex, sl::NoTimeout));
        FlushTextRendererLocked(engine);
        if (!engine->panicMode)
            ReleaseMutex(&engine->mutex);
    }

    void FullRefreshTextRenderer(TextRenderer* engine)
    {
        NPK_ASSERT(engine != nullptr);

        NPK_ASSERT(AcquireMutex(&engine->mutex, sl::NoTimeout));

        FlushTextRendererLocked(engine);
        for (auto it = engine->adaptors.Begin(); it != engine->adaptors.End();
            ++it)
        {
            for (size_t i = 0; i < engine->cells.Size(); i++)
            {
                const size_t x = i % engine->size.x;
                const size_t y = i / engine->size.x;
                RenderCell(&*it, { x, y }, engine->cells[i]);
            }
        }

        ReleaseMutex(&engine->mutex);
    }

    void FullRefreshTextAdaptor(TextRenderer* engine, TextAdaptor* adaptor)
    {
        NPK_ASSERT(engine != nullptr);
        NPK_ASSERT(adaptor != nullptr);

        NPK_ASSERT(AcquireMutex(&engine->mutex, sl::NoTimeout));

        FlushTextRendererLocked(engine);
        for (size_t i = 0; i < engine->cells.Size(); i++)
        {
            const size_t x = i % engine->size.x;
            const size_t y = i / engine->size.x;
            RenderCell(adaptor, { x, y }, engine->cells[i]);
        }

        ReleaseMutex(&engine->mutex);
    }
}
