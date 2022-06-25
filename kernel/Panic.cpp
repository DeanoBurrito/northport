#include <Panic.h>
#include <Locks.h>
#include <Log.h>
#include <devices/LApic.h>
#include <devices/DeviceManager.h>
#include <scheduling/Scheduler.h>
#include <StackTrace.h>

namespace Kernel
{
    //defined in Log.cpp
    extern void RenderPanicImage();
    
    struct PanicData
    {
        const char* message;
        StoredRegisters overrideRegisters;
        bool useOverrideRegs;
        uint8_t responseCore; //which core is handling the panic response, all others halt
        char lock;
    };
    PanicData details;

    void InitPanic()
    {
        details.message = "";
        details.useOverrideRegs = false;
        sl::SpinlockRelease(&details.lock);
    }

    void SetPanicOverrideRegs(StoredRegisters* regs)
    {
        details.overrideRegisters = *regs;
        details.useOverrideRegs = true;
    }

    [[noreturn]]
    void Panic(const char* message)
    {
        sl::SpinlockAcquire(&details.lock);
        Scheduling::Scheduler::Global()->Suspend(true);
        details.message = message;
        details.responseCore = GetCoreLocal()->apicId;

        CPU::SetInterruptsFlag();
        asm volatile("int $0xFE");
        __builtin_unreachable();
    }

    void PanicInternal(StoredRegisters* regs)
    {
        if (details.useOverrideRegs)
            regs = &details.overrideRegisters;
        const Scheduling::Thread* currentThread = Scheduling::Thread::Current();

        if (details.responseCore != GetCoreLocal()->apicId)
            goto final_loop_no_log;
        
        //if we can, ensure we have the processes's page tables loaded here
        if (currentThread != nullptr)
            currentThread->Parent()->VMM()->MakeActive();

        //this core has already claimed the panic response, all other cores will jump straight to halt
        Devices::LApic::Local()->BroadcastIpi(INT_VECTOR_PANIC, false);

        LogEnableDest(LogDest::FramebufferOverwrite, true);
        LogFramebufferClear(0x804000); //clear and reset framebuffer, with a nice brown colour.

        if (currentThread != nullptr)
            Logf("Thread %u (core %u) triggered kernel panic.", LogSeverity::Info, currentThread != nullptr ? currentThread->Id() : -1, details.responseCore);
        else
            Logf("Core %u (no current thread) triggered kernel panic.", LogSeverity::Info, details.responseCore);
        
        Log("Reason: ", LogSeverity::Info);
        Log(details.message, LogSeverity::Info);

        Log("---- General Registers: ----", LogSeverity::Info);
        Logf("rax: 0x%0lx, rbx: 0x%0lx, rcx: 0x%0lx, rdx: 0x%0lx", LogSeverity::Info, regs->rax, regs->rbx, regs->rcx, regs->rdx);
        Logf("rsi: 0x%0lx, rdi: 0x%0lx, rsp: 0x%0lx, rbp: 0x%0lx", LogSeverity::Info, regs->rsi, regs->rdi, regs->rsp, regs->rbp);
        Logf(" r8: 0x%0lx,  r9: 0x%0lx, r10: 0x%0lx, r11: 0x%0lx", LogSeverity::Info, regs->r8, regs->r9, regs->r10, regs->r11);
        Logf("r12: 0x%0lx, r13: 0x%0lx, r14: 0x%0lx, r15: 0x%0lx", LogSeverity::Info, regs->r12, regs->r13, regs->r14, regs->r15);

        Log("---- Control Registers: ----", LogSeverity::Info);
        Logf("cr0: 0x%0lx, cr2: 0x%0lx, cr3: 0x%0lx, cr4: 0x%0lx", LogSeverity::Info, ReadCR0(), ReadCR2(), ReadCR3(), ReadCR4());
        Logf("EFER: 0x%0lx", LogSeverity::Info, CPU::ReadMsr(MSR_IA32_EFER));

        Log("---- Return Frame: ----", LogSeverity::Info);
        Logf("stack: 0x%x:0x%lx", LogSeverity::Info, regs->iret_ss, regs->iret_rsp);
        Logf("code:  0x%x:0x%lx", LogSeverity::Info, regs->iret_cs, regs->iret_rip);
        Logf("flags: 0x%lx, ec: 0x%lx", LogSeverity::Info, regs->iret_flags, regs->errorCode);

        if (currentThread == nullptr)
            goto final_loop;

        Log("---- Virtual Memory Ranges: ----", LogSeverity::Info);
        currentThread->Parent()->VMM()->PrintRanges(ReadCR2());

        Log("---- Process & Thread: ----", LogSeverity::Info);
        Logf("threadId: %u, threadName: %s", LogSeverity::Info, currentThread->Id(), currentThread->Name().C_Str());
        Logf("threadFlags: 0x%x, threadState: 0x%x", LogSeverity::Info, (size_t)currentThread->Flags(), (size_t)currentThread->State());
        Logf("processId: %u, processName: %s", LogSeverity::Info, currentThread->Parent()->Id(), currentThread->Parent()->Name().C_Str());

        RenderPanicImage();

        //TODO: would be nice to print details about current thread (id, name, sibling processes. Basic code/data locations)
        //and a stack trace of course!
    final_loop:
        Log("Panic complete. All cores halted indefinitely until manual system reset.", LogSeverity::Info);
    final_loop_no_log:
        while (1)
            CPU::Halt();
    }
}
