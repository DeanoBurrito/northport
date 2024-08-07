#include <arch/m68k/GfPic.h>
#include <debug/Log.h>
#include <io/IntrRouter.h>
#include <memory/Vmm.h>

namespace Npk
{
    constexpr size_t PicCount = 6;
    constexpr size_t PicPins = 32;
    constexpr uintptr_t PicRegsBase = 0xFF00'0000; //TODO: get from bootloader
    constexpr size_t PicRegsStride = 0x1000;
    constexpr size_t PicVectorBase = 25;
    constexpr size_t RedirectTableSize = PicCount * PicPins;

    enum PicReg
    {
        Status = 0x00,
        Pending = 0x04, //bitset of pending interrupts
        ClearAll = 0x08,
        Disable = 0x0C, //bitset
        Enable = 0x10, //bitset
    };

    sl::NativePtr regsAccess;
    size_t redirectTable[RedirectTableSize];

    void InitPics()
    {
        auto maybeAccess = VMM::Kernel().Alloc(PicCount * PicRegsStride, PicRegsBase, VmFlag::Mmio | VmFlag::Write);
        ASSERT_(maybeAccess.HasValue());
        regsAccess = *maybeAccess;
        Log("Goldfish PICs mapped at %p (paddr=0x%tx)", LogLevel::Info, regsAccess.ptr, PicRegsBase);

        for (size_t i = 0; i < PicCount; i++)
        {
            sl::NativePtr regs = regsAccess.Offset(i * PicRegsStride);
            regs.Offset(PicReg::Disable).Write<uint32_t>(0);
            regs.Offset(PicReg::ClearAll).Write<uint32_t>(0); //clear any previously pending interrupts
        }

        for (size_t i = 0; i < RedirectTableSize; i++)
            redirectTable[i] = 0;
    }

    void HandlePicInterrupt(size_t vector)
    {
        vector -= PicVectorBase;
        ASSERT_(vector < 7);

        sl::NativePtr regs = regsAccess.Offset(vector * PicRegsStride);
        uint32_t irq = regs.Offset(PicReg::Pending).Read<uint32_t>();
        while (irq != 0)
        {
            const size_t pin = __builtin_ctz(irq);
            irq &= ~(1 << pin);

            const size_t redirectIndex = pin + (vector * PicPins);
            Io::InterruptRouter::Global().Dispatch(redirectTable[redirectIndex]);
        }
    }

    bool RoutePinInterrupt(size_t core, size_t vector, size_t gsi)
    {
        ASSERT_(core == 0);
        ASSERT_(gsi < RedirectTableSize);

        redirectTable[gsi] = vector;
        const size_t pic = gsi / PicPins;
        const size_t irq = gsi % PicPins;
        regsAccess.Offset(pic * PicRegsStride).Offset(PicReg::Enable).Write<uint32_t>(1 << irq);

        return true;
    }
}
