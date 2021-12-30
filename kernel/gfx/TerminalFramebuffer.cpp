#include <gfx/TerminalFramebuffer.h>
#include <Maths.h>
#include <Platform.h>

namespace Kernel::Gfx
{  
    void TerminalFramebuffer::RenderPsf1Text(const string& str, Vector2u charPos)
    {
        constexpr size_t fontWidth = 8;
        const size_t fontHeight = v1Font->charSize;
        sl::NativePtr glyphBuffer = sl::NativePtr((size_t)v1Font).As<void>(4);
        Vector2u start = charPos == AtCursorPos ? cursorPosition : charPos;
        
        for (size_t i = 0; i < str.Size(); i++)
        {
            uint8_t* glyph = glyphBuffer.As<uint8_t>(str[i] * v1Font->charSize);
            
            for (size_t y = 0; y < fontHeight; y++)
            {
                for (size_t x = 0; x < fontWidth; x++)
                {
                    if ((*glyph & (0b10000000 >> x)) == 0)
                        continue;

                    const size_t dx = (start.x + i) * currentFontSize.x + x;
                    const size_t dy = start.y * currentFontSize.y + y;
                    backingFb->DrawPixel({dx, dy}, fgColour);
                }

                //next line
                glyph++;
            }
        }
    }

    void TerminalFramebuffer::RenderPsf2Text(const string& str, Vector2u charPos)
    {}
    
    TerminalFramebuffer::TerminalFramebuffer()
    {
        backingFb = Devices::SimpleFramebuffer::Global();
        v1Font = psf1Default;
        v2Font = psf2Default;
        SetTextRenderVersion(1);

        //get screen size + default render region, and font size TODO:
        screenSize = backingFb->Size();
        SetReservedRegion({0, 0, (long)screenSize.x, (long)screenSize.y});

        bgColour = Colours::Black;
        fgColour = Colours::White;
        
        cursorSize = {8, 16};
        cursorPosition = {0, 0};
        cursorVisible = true;
    }

    void TerminalFramebuffer::SetTextRenderVersion(size_t version)
    { 
        useV2Font = version != 1;
        
        if (useV2Font)
            currentFontSize = { v2Font->charWidth, v2Font->charHeight };
        else
            currentFontSize = { 8, v1Font->charSize };
    }

    Devices::SimpleFramebuffer* TerminalFramebuffer::GetBackingBuffer() const
    { return backingFb; }

    IntRect TerminalFramebuffer::GetReservedRegion() const
    { return renderRegion; }

    void TerminalFramebuffer::SetReservedRegion(IntRect rect)
    {
        renderRegion.left = rect.left;
        renderRegion.top = rect.top;
        renderRegion.width = sl::min(rect.width, (long)screenSize.x - renderRegion.left);
        renderRegion.height = sl::min(rect.height, (long)screenSize.y - renderRegion.height);
    }

    Vector2u TerminalFramebuffer::GetCursorPos() const
    { return cursorPosition; }

    void TerminalFramebuffer::SetCursorPos(Vector2u newPosition)
    { 
        if (screenSize.x < newPosition.x)
            newPosition.y = screenSize.x - 1;
        if (screenSize.y < newPosition.y)
            newPosition.y = screenSize.y - 1;
        cursorPosition = newPosition; 
    }

    void TerminalFramebuffer::OffsetCursorPos(Vector2u offset)
    {
        cursorPosition.x += offset.x;
        cursorPosition.y += offset.y;
        if (screenSize.x < cursorPosition.x)
            cursorPosition.x = screenSize.x - 1;
        if (screenSize.y < cursorPosition.y)
            cursorPosition.y = screenSize.y - 1;
    }

    Vector2u TerminalFramebuffer::GetCursorSize() const
    { return cursorSize; }

    void TerminalFramebuffer::SetCursorSize(Vector2u size)
    {
        if (currentFontSize.x < size.x)
            size.x = currentFontSize.x;
        if (currentFontSize.y < size.y)
            size.y = currentFontSize.y;
        cursorSize = size;
    }

    bool TerminalFramebuffer::GetCursorVisible() const
    { return cursorVisible; }

    void TerminalFramebuffer::SetCursorVisible(bool visible)
    { cursorVisible = visible; }

    Colour TerminalFramebuffer::GetColour(bool isBackground) const
    { return isBackground ? bgColour : fgColour; }

    void TerminalFramebuffer::SetColour(bool isBackground, Colour col)
    {
        (isBackground ? bgColour : fgColour) = col;
    }

    void TerminalFramebuffer::ClearScreen() const
    { backingFb->Clear(bgColour); }

    void TerminalFramebuffer::ClearLine(size_t line) const
    {
        IntRect area = {0, (long)(line * currentFontSize.y), (long)currentFontSize.x, (long)currentFontSize.y};
        backingFb->DrawRect(area, bgColour, true);
    }
    
    void TerminalFramebuffer::Print(const string& text, const Vector2u where)
    {
        useV2Font ? RenderPsf2Text(text, where) : RenderPsf1Text(text, where);
        OffsetCursorPos({text.Size(), 0});
    }
    
    void TerminalFramebuffer::PrintLine(const string& text, const Vector2u where)
    {
        Print(text, where);
        OffsetCursorPos({-cursorPosition.x, 1});
    }
    
    void TerminalFramebuffer::PrintAndWrap(const string& text, const Vector2u where)
    {
        SetCursorPos(where);
        for (size_t i = 0; i < text.Size(); i += renderRegion.width)
        {
            PrintLine(text.SubString(i, renderRegion.width));
        }
    }
}
