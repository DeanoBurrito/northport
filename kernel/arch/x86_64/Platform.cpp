#include <arch/Platform.h>
#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Cpuid.h>
#include <debug/Log.h>
#include <Maths.h>
#include <Memory.h>

namespace Npk
{
    constexpr const char ArchPanicStr[] = "xsaveBits=0x%" PRIx64", extRegsSize=0x%zu\r\n";

    void ArchPrintPanicInfo(void (*Print)(const char* format, ...))
    {
        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::ArchConfig] == nullptr)
            return;

        auto cfg = static_cast<const ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig]);
        Print(ArchPanicStr, cfg->xSaveBitmap, cfg->xSaveBufferSize);
    }

    void ExplodeKernelAndReset()
    {
        struct [[gnu::packed, gnu::aligned(8)]]
        {
            uint16_t limit = 0;
            uint64_t base = 0;
        } idtr;

        asm volatile("sidt %0" :: "m"(idtr) : "memory");
        idtr.limit = 0;
        asm volatile("lidt %0; int $0" :: "m"(idtr));
    }

    struct ExtendedRegs {};

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user)
    {
        sl::memset(frame, 0, sizeof(TrapFrame));

        frame->iret.cs = user ? SelectorUserCode : SelectorKernelCode;
        frame->iret.ss = user ? SelectorUserData : SelectorKernelData;
        frame->iret.rsp = sl::AlignDown(stack, 16);
        frame->iret.rip = entry;
        frame->iret.flags = 0x202;
        frame->rbp = 0;
    }

    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value)
    {
        const uint64_t val = reinterpret_cast<uint64_t>(value);
        switch (index)
        {
        case 0: 
            frame->rdi = val; 
            return;
        case 1: 
            frame->rsi = val; 
            return;
        case 2: 
            frame->rdx = val; 
            return;
        case 3: 
            frame->rcx = val; 
            return;
        case 4: 
            frame->r8 = val; 
            return;
        case 5: 
            frame->r9 = val; 
            return;
        }
    }

    void* GetTrapFrameArg(TrapFrame* frame, size_t index)
    {
        uint64_t value = 0;
        switch (index)
        {
        case 0:
            value = frame->rdi;
            break;
        case 1:
            value = frame->rsi;
            break;
        case 2:
            value = frame->rdx;
            break;
        case 3:
            value = frame->rcx;
            break;
        case 4:
            value = frame->r8;
            break;
        case 5:
            value = frame->r9;
            break;
        }

        return reinterpret_cast<void*>(value);
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    {
        constexpr size_t FxSaveBufferSize = 512;
        const ArchConfig* config = static_cast<ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig]);
        const size_t bufferSize = sl::Max(config->xSaveBufferSize, FxSaveBufferSize);

        *regs = reinterpret_cast<ExtendedRegs*>(new uint8_t[bufferSize]);
        sl::memset(*regs, 0, bufferSize);
    }

    void SaveExtendedRegs(ExtendedRegs* regs)
    {
        WriteCr0(ReadCr0() & ~(1ul << 3));

        const ArchConfig* config = static_cast<ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig]);
        if (config->xSaveBitmap != 0)
            asm volatile("xsave %0" :: "m"(*regs), "a"(config->xSaveBitmap), 
                "d"(config->xSaveBitmap >> 32) : "memory");
        else
            asm volatile("fxsave %0" :: "m"(*regs) : "memory");
    }

    void LoadExtendedRegs(ExtendedRegs* regs)
    {
        WriteCr0(ReadCr0() & ~(1ul << 3));

        const ArchConfig* config = static_cast<ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig]);
        if (config->xSaveBitmap != 0)
            asm volatile("xrstor %0" :: "m"(*regs), "a"(config->xSaveBitmap), "d"(config->xSaveBitmap >> 32));
        else 
            asm volatile("fxrstor %0" :: "m"(*regs));
    }

    void ExtendedRegsFence()
    {
        WriteCr0(ReadCr0() | (1 << 3));
    }

    uintptr_t GetReturnAddr(size_t level, uintptr_t start)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));
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

    void SetHardwareRunLevel(RunLevel rl)
    {
        switch (rl)
        {
        case RunLevel::Interrupt:
        case RunLevel::Clock:
            return WriteCr8(0xF);
        //TODO: conditional disabling of interrupts at other levels (and enabling interrupts)
        default:
            return WriteCr8(0);
        }
    }

    bool RoutePinInterrupt(size_t core, size_t vector, size_t gsi)
    {
        uint8_t pin = gsi;
        return IoApic::Route(pin, vector, core, {}, {}, false);
    }
}
