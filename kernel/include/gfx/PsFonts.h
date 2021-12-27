#pragma once

#include <stdint.h>
#include <stddef.h>
#include <devices/SimpleFramebuffer.h>

extern uint8_t font1DefaultData[];
extern uint8_t font2DefaultData[];

namespace Kernel::Gfx
{
    constexpr static inline uint8_t Psf1Magic[] = { 0x36, 0x04 };
    constexpr static inline size_t Psf1MagicLen = 2;
    
    enum class Psf1ModeFlags : uint8_t
    {
        Has512Glyphs = (1 << 0),
        HasTab = (1 << 1),
        HasSeq = (1 << 2),
    };

    constexpr static inline uint16_t Psf1Separator = 0xFFFF;
    constexpr static inline uint16_t Psf1StartSeq = 0xFFFE;
    
    struct [[gnu::packed]] Psf1
    {
        uint8_t magic[2];
        Psf1ModeFlags mode;
        uint8_t charSize; //number of bytes per char, width is always 8, so this number is the height
    };

    constexpr static inline uint8_t Psf2Magic[] = { 0x72, 0xB5, 0x4A, 0x86 };
    constexpr static inline size_t Psf2MagicLen = 4;
    constexpr static inline uint32_t Psf2HasUnicodeTable = 1 << 0;
    constexpr static inline uint32_t Psf2MaxVersion = 0;
    constexpr static inline uint8_t Psf2Separator = 0xFF;
    constexpr static inline uint8_t Psf2StartSeq = 0xFE;

    struct [[gnu::packed]] Psf2
    {
        uint8_t magic[4];
        uint32_t version;
        uint32_t bitmapOffset;
        uint32_t flags;
        uint32_t numGlyphs;
        uint32_t bytesPerCharacter;
        uint32_t charHeight;
        uint32_t charWidth;
    };

    const static inline Psf1* psf1Default = (Psf1*)font1DefaultData;
    const static inline Psf2* psf2Default = (Psf2*)font2DefaultData;
}