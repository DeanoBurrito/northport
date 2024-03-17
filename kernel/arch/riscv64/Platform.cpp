#include <arch/Platform.h>
#include <arch/riscv64/Sbi.h>
#include <Maths.h>
#include <stdint.h>

namespace Npk
{
    enum class ExtState : uint8_t
    {
        Off = 0,
        Initial = 1,
        Clean = 2,
        Dirty = 3,
    };

    struct ExtendedRegs
    {
        union
        {
            struct
            {
                ExtState fpu;
                ExtState vector;
            };
            uintptr_t alignment;
        } state;

        uint8_t buffer[];
    };

    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user)
    {
        frame->flags.spie = 1;
        frame->flags.spp = user ? 0 : 1;
        frame->sepc = entry;
        frame->sp = sl::AlignDown(stack, 8);
        frame->fp = 0;
    }

    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value)
    {
        if (index >= TrapFrameArgCount)
            return;

        uint64_t* args = &frame->a0;
        args[index] = reinterpret_cast<uint64_t>(value);
    }

    void* GetTrapFrameArg(TrapFrame* frame, size_t index)
    {
        if (index >= TrapFrameArgCount)
            return nullptr;

        uint64_t* args = &frame->a0;
        return reinterpret_cast<void*>(args[index]);
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    { (void)regs; } //TODO: implement meeeee

    void SaveExtendedRegs(ExtendedRegs* regs)
    { (void)regs; }

    void LoadExtendedRegs(ExtendedRegs* regs)
    { (void)regs; }

    void ExtendedRegsFence()
    {}

    uintptr_t GetReturnAddr(size_t level)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        //NOTE: this is only possible because we compile with -fno-omit-framepointer.
        //otherwise we'd have to make use of EH/unwind metadata (no thanks).
        Frame* current = reinterpret_cast<Frame*>((uintptr_t)__builtin_frame_address(0) - 16);
        for (size_t i = 0; i <= level; i++)
        {
            if (current == nullptr)
                return 0;
            if (i == level)
                return current->retAddr;
            current = current->next - 1;
        }
        return 0;
    }

    void SendIpi(size_t dest)
    {
        //SBI spec doesn't specify SXLEN-alignment, but some platforms expect it.
        //so we do it anyway, just in case.
        SbiSendIpi(1ul << (dest % 64), dest / 64);
    }
}
