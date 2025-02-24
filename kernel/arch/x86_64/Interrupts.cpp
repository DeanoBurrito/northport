#include <arch/Interrupts.h>
#include <arch/Misc.h>
#include <arch/Entry.h>
#include <arch/x86_64/Apic.h>
#include <Panic.h>

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
        uint64_t ds;
        uint64_t es;

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

    extern "C" void InterruptDispatch(TrapFrame* frame)
    {
        if (CoreLocalAvailable() && GetLocalPtr(SubsysPtr::UnsafeOpAbort) != nullptr)
        {
            frame->iret.rip = reinterpret_cast<uint64_t>(GetLocalPtr(SubsysPtr::UnsafeOpAbort));
            SetLocalPtr(SubsysPtr::UnsafeOpAbort, nullptr);
            return;
        }

        using namespace Core;
        const RunLevel prevRl = RaiseRunLevel(RunLevel::Interrupt);

        switch (frame->vector)
        {
        case 0x2:
            PanicWithString("Received NMI");
        case 0xE:
            {
                PageFaultFrame pf {};
                pf.address = ReadCr2();
                pf.write = frame->ec & (1 << 1);
                pf.user = frame->ec & (1 << 2);
                pf.fetch = frame->ec & (1 << 4);
                DispatchPageFault(&pf);
                break;
            }
        case IntrVectorTimer:
            DispatchAlarm();
            break;
        case IntrVectorIpi:
            DispatchIpi();
            break;
        default:
            if (frame->vector >= 32)
                DispatchInterrupt(frame->vector - 32);
            else
            {
                ExceptionFrame ex;
                ex.pc = frame->iret.rip;
                ex.stack = frame->iret.rsp;
                ex.archId = frame->vector;
                ex.archFlags = frame->ec;
                DispatchException(&ex);
            }
            break;
        }
        //TODO: DispatchSyscall()

        if (frame->vector >= 32)
            static_cast<LocalApic*>(GetLocalPtr(SubsysPtr::IntrCtrl))->SendEoi();
        LowerRunLevel(prevRl);
    }
}
