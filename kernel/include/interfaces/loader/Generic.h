#pragma once

#include <Optional.h>
#include <Span.h>

/* I want to make a point about this abstraction: the main reason the boot protocol is abstracted
 * from the kernel is because I intend to support 32-bit platforms, which the limine protocol
 * does not support idealogically. Of course, I've done this (see my original m68k port and it's
 * homebrewed limine protocol loader), but there are easier ways to go about it.
 * So while I'm not a big fan of this, I do think it's necessary for my use case. If you're here
 * because you're exploring the northport source code and hoping to learn something, this isn't it.
 */
namespace Npk::Loader
{
    struct MemmapEntry
    {
        Paddr base;
        size_t length;
    };

    struct LoaderData
    {
        uintptr_t directMapBase;
        size_t directMapLength;
        Paddr kernelPhysBase;
        sl::Opt<Paddr> rsdp;
        sl::Opt<Paddr> fdt;

        struct
        {
            Paddr address;
            size_t stride;
            uint32_t width;
            uint32_t height;
            uint8_t redShift;
            uint8_t blueShift;
            uint8_t greenShift;
            uint8_t redBits;
            uint8_t greenBits;
            uint8_t blueBits;
            bool valid;
        } framebuffer;
    };

    void GetData(LoaderData& data);
    sl::StringSpan GetCommandLine();
    size_t GetMemmapUsable(sl::Span<MemmapEntry> store, size_t offset);
}
