#pragma once

#include <LinearFramebuffer.h>
#include <PcScreenFont.h>
#include <String.h>
#include <Vectors.h>
#include <Rects.h>

namespace np::Graphics
{
    class TerminalFramebuffer
    {
    private:
        const Psf1* v1Font;
        const Psf2* v2Font;
        bool useV2Font;

        LinearFramebuffer* backingFb;
        sl::Vector2u screenSize;
        sl::Vector2u currentFontSize;
        Colour bgColour;
        Colour fgColour;

        sl::UIntRect renderRegion;

        sl::Vector2u cursorSize;
        sl::Vector2u cursorPosition;
        bool cursorVisible;

        void RenderPsf1Text(const string& str, sl::Vector2u charPos);
        void RenderPsf2Text(const string& str, sl::Vector2u charPos);
    public:
        constexpr static inline sl::Vector2u AtCursorPos = sl::Vector2u{ (size_t)-1, (size_t)-1 };

        TerminalFramebuffer(LinearFramebuffer* backingFramebuffer);

        TerminalFramebuffer(const TerminalFramebuffer&) = delete;
        TerminalFramebuffer& operator=(const TerminalFramebuffer&) = delete;
        TerminalFramebuffer(TerminalFramebuffer&&) = delete;
        TerminalFramebuffer operator=(TerminalFramebuffer&&) = delete;

        void SetTextRenderVersion(size_t version);
        LinearFramebuffer* GetBackingBuffer() const;

        sl::UIntRect GetRenderRegion() const;
        void SetRenderRegion(sl::UIntRect region);

        sl::Vector2u GetCursorPos() const;
        void SetCursorPos(sl::Vector2u newPosition);
        void OffsetCursorPos(sl::Vector2u offset);
        sl::Vector2u GetCursorSize() const;
        void SetCursorSize(sl::Vector2u size);
        bool GetCursorVisible() const;
        void SetCursorVisible(bool visible);

        Colour GetColour(bool isBackground) const;
        void SetColour(bool isBackground, Colour col);

        void ClearScreen() const;
        void ClearLine(size_t line) const;
        void Print(const string& text, const sl::Vector2u where = AtCursorPos);
        void PrintLine(const string& text, const sl::Vector2u where = AtCursorPos);
        void PrintAndWrap(const string& text, const sl::Vector2u where = AtCursorPos);
    };
}
