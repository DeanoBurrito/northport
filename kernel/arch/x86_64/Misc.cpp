#include <arch/Misc.h>
#include <core/Log.h>
#include <Memory.h>
#include <Maths.h>

#define FXSAVE(regs) do { asm("fxsave %0" :: "m"(regs) : "memory"); } while (false)
#define FXRSTOR(regs) do { asm("fxrstor %0" :: "m"(regs)); } while (false)
#define XSAVE(regs, bitmap) do { asm("xsave %0" :: "m"(regs), "a"(bitmap & 0xFFFF'FFFF), "d"(bitmap >> 32) : "memory"); } while (false)
#define XRSTOR(regs, bitmap) do { asm("xrstor %0" :: "m"(regs), "a"(bitmap & 0xFFFF'FFFF), "d"(bitmap >> 32)); } while (false)

namespace Npk
{
    constexpr uint64_t Cr0TsFlag = 1 << 3;
    constexpr size_t FxsaveSize = 512;
    
    struct ExtendedRegs
    {
        uint8_t buffer[]; //size of this buffer is stored in CoreLocalBlock().xsaveSize
    };

    void ExplodeKernelAndReset()
    {
        struct SL_PACKED(
        {
            uint16_t limit = 0;
            uint64_t base = 0;
        }) idtr;

        asm volatile("lidt %0; int $0" :: "m"(idtr));
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        VALIDATE_(regs != nullptr, );

        const size_t bufferSize = sl::Max(CoreLocalBlock().xsaveSize, FxsaveSize);
        *regs = reinterpret_cast<ExtendedRegs*>(new uint8_t[bufferSize]);
        if (*regs != nullptr)
            sl::memset(*regs, 0, bufferSize);
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        VALIDATE_(regs != nullptr, );

        WriteCr0(ReadCr0() & ~Cr0TsFlag);
        if (CoreLocalBlock().xsaveBitmap == 0)
            FXSAVE(*regs);
        else
            XSAVE(*regs, CoreLocalBlock().xsaveBitmap);
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        VALIDATE_(regs != nullptr, );

        WriteCr0(ReadCr0() & ~Cr0TsFlag);
        if (CoreLocalBlock().xsaveBitmap == 0)
            FXRSTOR(*regs);
        else
            XRSTOR(*regs, CoreLocalBlock().xsaveBitmap);
    }

    bool ExtendedRegsFence()
    {
        WriteCr0(ReadCr0() | Cr0TsFlag);
        return true;
    }

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        size_t count = 0;
        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));

        for (size_t i = 0; i < offset; i++)
        {
            if (current == 0)
                return count;
            current = current->next;
        }

        for (size_t i = 0; i < store.Size(); i++)
        {
            if (current == nullptr)
                return count;
            store[count++] = current->retAddr;
            current = current->next;
        }

        return count;
    }

    void PoisonMemory(sl::Span<uint8_t> range)
    {
        //0xCC is `int3` instruction, it generates a breakpoint exception.
        sl::memset(range.Begin(), 0xCC, range.Size());
    }
}
