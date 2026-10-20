#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <Core.hpp>

namespace Npk
{
    static void NotifyOfBadOp() { NPK_UNREACHABLE(); } //TODO: process + personas

    SL_TAGGED(cpubase, CoreLocalHeader localHeader);

    static void SetFrameForJacket(TrapFrame* frame, HwExceptionFlag which)
    {
        NPK_ASSERT(localHeader.ExceptRecoveryPc != nullptr);
        NPK_ASSERT(localHeader.exceptRecoveryStack != nullptr);

        frame->iret.rip = (uint64_t)localHeader.ExceptRecoveryPc;
        frame->rdi = (uint64_t)localHeader.exceptRecoveryStack;

        localHeader.exceptFlags.Reset();
        localHeader.exceptFlags.Set(which);
    }

    extern "C" void InterruptDispatch(TrapFrame* frame)
    {
        RemoteStatus(MyCoreId())->lastIntrFrame = frame;

        if (frame->vector == 0x1 || frame->vector == 0x3)
            return HandleDebugException(frame, frame->vector == 3);

        auto prevIpl = RaiseIpl(Ipl::Interrupt);

        bool suppressEoi = false;
        switch (frame->vector)
        {
        case 0x0: //divide error
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x2: //NMI, we dont currently use these
            NPK_UNREACHABLE();

        case 0x4: //overflow exception
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x5: //BOUND range exceeded
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x6: //invalid opcode
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x7: //device (fpu/vpu) not available
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x8: //double fault :(
            Panic("Double fault occured", frame);

        case 0xC: //stack segment fault
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0xD: //general protection fault
            if (localHeader.exceptFlags.Has(HwExceptionFlag::BadIo))
                SetFrameForJacket(frame, HwExceptionFlag::BadIo);
            else
                NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0xE: //page fault
            if (localHeader.exceptFlags.Has(HwExceptionFlag::PageFault))
                SetFrameForJacket(frame, HwExceptionFlag::PageFault);
            else if (prevIpl != Ipl::Passive)
            {
                Panic("Page fault at non-passive IPL, addr=0x%tx, ec=0x%tx", 
                    frame, READ_CR(2), frame->ec);
            }
            else
            {
                const bool write = frame->ec & 0b10;
                const bool user = frame->ec & 0b100;

                bool isBadFault = false;
                bool* probePtr = nullptr;
                if (localHeader.exceptFlags.Has(HwExceptionFlag::BadPageFault))
                    probePtr = &isBadFault;

                DispatchPageFault(probePtr, READ_CR(2), write, user);

                if (probePtr != nullptr && isBadFault)
                    SetFrameForJacket(frame, HwExceptionFlag::BadPageFault);
            }
            suppressEoi = true;
            break;

        case 0x10: //x87 error
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case 0x12: //machine check exception
            HandleMachineCheckException(frame);
            suppressEoi = true;
            break;

        case 0x13: //simd exception
            NotifyOfBadOp();
            suppressEoi = true;
            break;

        case LapicSpuriousVector:
            suppressEoi = true;
            break;

        case LapicErrorVector:
            HandleLapicErrorInterrupt();
            break;

        case LapicTimerVector:
            HandleAlarmInterrupt();
            break;

        case LapicIpiVector:
            DispatchIpi();
            break;

        default:
            if (frame->vector < 0x20)
                Panic("Illegal exception occurred", frame);
            DispatchInterrupt(frame->vector - 0x20);
            break;
        }
        
        RemoteStatus(MyCoreId())->lastIntrFrame = nullptr;
        if (!suppressEoi)
            SignalEoi();
        LowerIpl(prevIpl);
    }

    extern "C" void SyscallDispatch()
    {
        IntrsOn();

        IntrsOff();
        NPK_UNREACHABLE();
    }
}
