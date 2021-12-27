#pragma once

#include <devices/SimpleFramebuffer.h>
#include <gfx/PsFonts.h>
#include <String.h>

namespace Kernel::Gfx
{
    class TerminalFramebuffer
    {
    private:
        const Psf1* v1Font;
        const Psf2* v2Font;
        bool useV2Font;

        Devices::SimpleFramebuffer* backingFb;
        Vector2u screenSize;
        Vector2u currentFontSize;
        Colour bgColour;
        Colour fgColour;
        IntRect renderRegion;

        Vector2u cursorSize;
        Vector2u cursorPosition;
        bool cursorVisible;

        void RenderPsf1Text(const string& str, Vector2u charPos);
        void RenderPsf2Text(const string& str, Vector2u charPos);
    public:
        constexpr static inline Vector2u AtCursorPos = Vector2u{ (size_t)-1, (size_t)-1 };

        TerminalFramebuffer();

        TerminalFramebuffer(const TerminalFramebuffer&) = delete;
        TerminalFramebuffer& operator=(const TerminalFramebuffer&) = delete;
        TerminalFramebuffer(TerminalFramebuffer&&) = delete;
        TerminalFramebuffer operator=(TerminalFramebuffer&&) = delete;

        void SetTextRenderVersion(size_t version);
        Devices::SimpleFramebuffer* GetBackingBuffer() const;

        IntRect GetReservedRegion() const;
        void SetReservedRegion(IntRect rect);

        Vector2u GetCursorPos() const;
        void SetCursorPos(Vector2u newPosition);
        void OffsetCursorPos(Vector2u offset);
        Vector2u GetCursorSize() const;
        void SetCursorSize(Vector2u size);
        bool GetCursorVisible() const;
        void SetCursorVisible(bool visible);

        Colour GetColour(bool isBackground) const;
        void SetColour(bool isBackground, Colour col);

        void ClearScreen() const;
        void ClearLine(size_t line) const;
        void Print(const string& text, const Vector2u where = AtCursorPos);
        void PrintLine(const string& text, const Vector2u where = AtCursorPos);
        void PrintAndWrap(const string& text, const Vector2u where = AtCursorPos);
    };
}
