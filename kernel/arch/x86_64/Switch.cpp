#include <arch/Switch.h>
#include <Memory.h>
#include <Maths.h>

asm(R"(
.global SwitchExecFrame

.type SwitchExecFrame, @function
.size SwitchExecFrame, (_EndOfSwitchExecFrame - SwitchExecFrame)

.pushsection .text
SwitchExecFrame:
    test %rdi, %rdi ## skip saving current state if `store` is null
    jz 1f

    ## rip is already on the stack from calling this function
    pushfq
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbp
    push %rbx
    push %rdi

    mov %rsp, (%rdi)
1:
## switch stacks, run callback if non-null
    mov %rsi, %rsp
    test %rdx, %rdx
    jz 1f

    mov %rcx, %rdi
    call *%rdx
1:
## load next state
    pop %rdi
    pop %rbx
    pop %rbp
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    popfq
    ret
_EndOfSwitchExecFrame:
.popsection
)");

namespace Npk
{
    struct ExecFrame
    {
        uint64_t rdi;
        uint64_t rbx;
        uint64_t rbp;
        uint64_t r12;
        uint64_t r13;
        uint64_t r14;
        uint64_t r15;
        uint64_t flags;
        uint64_t rip;
    };

    ExecFrame* InitExecFrame(uintptr_t stack, uintptr_t entry, void* arg)
    {
        ExecFrame* frame = reinterpret_cast<ExecFrame*>(
            sl::AlignDown(stack - sizeof(ExecFrame), alignof(ExecFrame)));

        frame->rip = entry;
        frame->flags = 0x202;
        frame->rbx = 0;
        frame->rbp = 0;
        frame->r12 = 0;
        frame->r13 = 0;
        frame->r14 = 0;
        frame->r15 = 0;
        frame->rdi = reinterpret_cast<uint64_t>(arg);

        return frame;
    }
}
