#include <arch/Interrupts.h>
#include <arch/Misc.h>
#include <arch/x86_64/Apic.h>
#include <core/Smp.h>
#include <core/Log.h>
#include <core/Clock.h>
#include <services/Program.h>
#include <services/Vmm.h>
#include <Panic.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk
{
    //SendIpi() and RoutePinInterrupt() are implemented in Apic.cpp

    sl::Opt<MsiConfig> ConstructMsi(size_t core, size_t vector)
    {
        MsiConfig cfg;
        cfg.address = ((core & 0xFF) << 12) | 0xFEE0'0000;
        cfg.data = vector & 0xFF;

        return cfg;
    }

    bool DeconstructMsi(MsiConfig cfg, size_t& core, size_t& vector)
    {
        core = (cfg.address >> 12) & 0xFF;
        vector = cfg.data & 0xFF;
        return true;
    }

    struct TrapFrame
    {
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rbp;
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;

        uint64_t vector;
        uint64_t ec;

        struct
        {
            uint64_t rip;
            uint64_t cs;
            uint64_t flags;
            uint64_t rsp;
            uint64_t ss;
        } iret;
    };

    static_assert(sizeof(TrapFrame) == 176, "x86_64 TrapFrame size changed, update assembly sources.");

    TrapFrame* InitTrapFrame(uintptr_t stack, uintptr_t entry, bool user)
    {
        TrapFrame* frame = reinterpret_cast<TrapFrame*>(stack) - 2;
        sl::memset(frame, 0, sizeof(TrapFrame) * 2);

        frame->iret.cs = user ? SelectorUserCode : SelectorKernelCode;
        frame->iret.ss = user ? SelectorUserData : SelectorKernelData;
        frame->iret.rsp = sl::AlignDown(stack, 16);
        frame->iret.rip = entry;
        frame->iret.flags = 0x202;
        frame->rbp = 0;

        return frame;
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

    static Services::ProgramException TranslateException(TrapFrame* frame)
    {
        using namespace Services;

        ProgramException ex {};
        ex.pc = frame->iret.rip;
        ex.stack = frame->iret.rsp;
        ex.special = frame->vector;
        ex.flags = frame->ec;

        switch (frame->vector)
        {
        case 0x1: ex.type = ExceptionType::Breakpoint; break; //debug exception
        case 0x3: ex.type = ExceptionType::Breakpoint; break; //breakpoint instruction (int3)
        case 0x6: ex.type = ExceptionType::BadOperation; break; //invalid opcode exception
        case 0xE: //page fault
            ex.type = ExceptionType::MemoryAccess; 
            ex.special = ReadCr2();
            break; 
        default: ex.type = ExceptionType::BadOperation; break;
        }

        return ex;
    }

    extern "C" void TrapDispatch(TrapFrame* frame)
    {
        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::UnsafeOpAbort) != nullptr)
        {
            frame->iret.rip = reinterpret_cast<uint64_t>(GetLocalPtr(SubsysPtr::UnsafeOpAbort));
            SetLocalPtr(SubsysPtr::UnsafeOpAbort, nullptr);
            SwitchFrame(nullptr, nullptr, frame, nullptr);
        }

        using namespace Core;
        const RunLevel prevRl = RaiseRunLevel(RunLevel::Interrupt);
        //TODO: if prevRl==Normal, stash interrupted register state

        if (frame->vector < 0x20)
        {
            using namespace Services;

            auto except = TranslateException(frame);
            bool handled = false;
            if (except.type == ExceptionType::MemoryAccess)
            {
                VmFaultFlags flags {};
                flags |= (frame->ec & (1 << 1)) ? VmFaultFlag::Write : VmFaultFlag::Read;
                flags |= (frame->ec & (1 << 2)) ? VmFaultFlag::User : VmFaultFlags {};
                flags |= (frame->ec & (1 << 4)) ? VmFaultFlag::Fetch : VmFaultFlags {};
                handled = KernelVmm().HandlePageFault(except.special, flags);
            }
            else if (frame->vector == 0x2)
            {
                //an NMI could be a ping to look at any number of things, say its been handled and try all of them
                handled = true;

                ProcessLocalMail();
            }

            if (!handled)
                PanicWithException(except, frame->rbp);
        }
        else
        {
            static_cast<LocalApic*>(GetLocalPtr(SubsysPtr::IntrCtrl))->SendEoi();

            if (frame->vector == IntrVectorIpi)
                ProcessLocalMail();
            else if (frame->vector == IntrVectorTimer)
                ProcessLocalClock();
            else
            {} //TODO: notify IntrRouter
        }

        LowerRunLevel(prevRl);
        DisableInterrupts();
        SwitchFrame(nullptr, nullptr, frame, nullptr);
        ASSERT_UNREACHABLE();
    }
}
