#include "Util.h"
#include <Maths.h>

#ifdef NPL_ENABLE_LOGGING
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0

#define NANOPRINTF_IMPLEMENTATION
#include <NanoPrintf.h>
#endif

namespace Npl
{
    void Panic(PanicReason r)
    {
        NPL_LOG("LOADER PANIC: %u", (unsigned)r);
        asm("clr %d1; add #0xDEAD, %d1");
        asm("clr %d2; add #0xDEAD, %d2");
        asm("clr %d3; add #0xDEAD, %d3");
        while (true)
            asm("stop #0x2700");
        __builtin_unreachable();
    }

    sl::CNativePtr FindBootInfoTag(BootInfoType type, sl::CNativePtr begin)
    {
        constexpr size_t ReasonableSearchCount = 50;

        if (begin.ptr == nullptr)
            begin = sl::AlignUp((uintptr_t)LOADER_BLOB_END, 2);

        for (size_t i = 0; i < ReasonableSearchCount; i++)
        {
            auto tag = begin.As<BootInfoTag>();
            if (tag->type == BootInfoType::Last)
                return nullptr;
            if (tag->type == type)
                return begin;
            begin = begin.Offset(tag->size);
        }

        return nullptr;
    }

#ifdef NPL_ENABLE_LOGGING
    sl::NativePtr uart = nullptr;

    void UartWrite(int c, void* ignored)
    {
        (void)ignored;
        if (uart.ptr != nullptr)
            uart.Write<uint32_t>(c); //registers are 32-bits wide
    }
#endif
}
