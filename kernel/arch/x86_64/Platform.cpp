#include <arch/Platform.h>
#include <arch/Cpu.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Apic.h>
#include <debug/Log.h>
#include <Maths.h>
#include <Memory.h>

namespace Npk
{
    struct ExtendedRegs {};

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, void* arg, bool user)
    {
        frame->iret.cs = user ? SelectorUserCode : SelectorKernelCode;
        frame->iret.ss = user ? SelectorUserData : SelectorKernelData;
        frame->rdi = (uint64_t)arg;
        frame->iret.rsp = sl::AlignDown(stack, 16);
        frame->iret.rip = entry;
        frame->iret.flags = 0x202;
        frame->rbp = 0;
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        constexpr size_t FxSaveBufferSize = 512;
        const CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        const size_t bufferSize = sl::Max(config->xSaveBufferSize, FxSaveBufferSize);

        *regs = reinterpret_cast<ExtendedRegs*>(new uint8_t[bufferSize]);
        sl::memset(*regs, 0, bufferSize);
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        sl::InterruptGuard intGuard;
        WriteCr0(ReadCr0() & ~(1ul << 3));

        const CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        if (config->xSaveBitmap != 0)
            asm volatile("xsave %0" :: "m"(*regs), "a"(config->xSaveBitmap), 
                "d"(config->xSaveBitmap >> 32) : "memory");
        else
            asm volatile("fxsave %0" :: "m"(*regs) : "memory");
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        sl::InterruptGuard intGuard;
        WriteCr0(ReadCr0() & ~(1ul << 3));

        const CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        if (config->xSaveBitmap != 0)
            asm volatile("xrstor %0" :: "m"(*regs), "a"(config->xSaveBitmap), "d"(config->xSaveBitmap >> 32));
        else 
            asm volatile("fxrstor %0" :: "m"(*regs));
    }

    void ExtendedRegsFence()
    {
        WriteCr0(ReadCr0() | (1 << 3));
    }

    uintptr_t GetReturnAddr(size_t level)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        Frame* current = static_cast<Frame*>(__builtin_frame_address(0));
        // asm ("mov %%rbp, %0" : "=r"(current) :: "memory");
        for (size_t i = 0; i <= level; i++)
        {
            if (current == nullptr)
                return 0;
            if (i == level)
                return current->retAddr;
            current = current->next;
        }

        return 0;
    }

    void SendIpi(size_t dest)
    {
        LocalApic::Local().SendIpi(dest);
    }
}
