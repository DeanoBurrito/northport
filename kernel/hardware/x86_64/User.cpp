#include <Hardware.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag UserHeapTag = NPK_MAKE_HEAP_TAG("User");

    struct UserFrame
    {
        uint64_t type;

        union
        {
            struct
            {
                uint64_t function;
                uint64_t arg0;
                uint64_t arg1;
                uint64_t arg2;
                uint64_t arg3;
                uint64_t arg4;
                uint64_t arg5;
                uint64_t userPc;
                uint64_t userSp;
            } syscall;
        };
    };

    void HwPrimeUserContext(HwUserContext* context, uintptr_t entry, 
        uintptr_t arg, uintptr_t stack)
    {
        NPK_CHECK(context != nullptr, );

        context->resumePc = entry;
        context->resumeSp = stack;

        void* framePtr = PoolAllocPaged(sizeof(UserFrame), UserHeapTag);
        NPK_CHECK(framePtr != nullptr, );

        context->frame = static_cast<UserFrame*>(framePtr);
        //TODO: rig up argument passing
    }

    void HwCleanupUserContext(HwUserContext* context)
    {
        NPK_CHECK(context != nullptr, );
        NPK_CHECK(context->frame != nullptr, );

        NPK_ASSERT(
            PoolFreePaged(context->frame, sizeof(UserFrame), UserHeapTag));
        context->frame = nullptr;
        context->resumePc = 0;
        context->resumeSp = 0;
    }

    HwUserExitInfo HwEnterUserContext(HwUserContext* context)
    {
        HwUserExitInfo exitInfo {};
        exitInfo.type = HwUserExitType::InvalidEntryState;

        if (context == nullptr)
            return exitInfo;
        if (context->frame == nullptr)
            return exitInfo;
        if (!HwIsCanonicalUserAddress(context->resumePc))
            return exitInfo;
        if (!HwIsCanonicalUserAddress(context->resumeSp))
            return exitInfo;

        //TODO: also populate tss->rsp0
        const uint64_t targetPc = context->resumePc;
        const uint64_t targetSp = context->resumeSp;
        const uint64_t frameAddr = reinterpret_cast<uint64_t>(context->frame);

        const bool prevIntrs = IntrsOff();
        asm volatile(R"(
            push %%rbx
            push %%rcx;
            push %%rdx;
            push %%rsi;
            push %%rdi;
            push %%rbp;
            push %%r8;
            push %%r9;
            push %%r10;
            push %%r11;
            push %%r12;
            push %%r13;
            push %%r14;
            push %%r15;

            push %0;
            lea 1f(%%rip), %%rax;
            push %%rax;
            mov %%rsp, %%gs:24;

            swapgs;
            mov $(0x30 | 3), %%eax;
            mov %%ax, %%ds;
            mov %%ax, %%es;
            mov %%ax, %%fs;
            mov %%ax, %%gs;

            push $(0x30 | 3);
            push %2;
            push $0x202;
            push $(0x28 | 3);
            push %1;

            xor %%rax, %%rax;
            xor %%rbx, %%rbx;
            xor %%rcx, %%rcx;
            xor %%rdx, %%rdx;
            xor %%rsi, %%rsi;
            xor %%rdi, %%rdi;
            xor %%rbp, %%rbp
            xor %%r8, %%r8;
            xor %%r9, %%r9;
            xor %%r10, %%r10;
            xor %%r11, %%r11;
            xor %%r12, %%r12;
            xor %%r13, %%r13;
            xor %%r14, %%r14;
            xor %%r15, %%r15;
            iretq;

        1:
            xor %%eax, %%eax;
            mov %%rax, %%gs:24;

            pop %%rax
            pop %%r15;
            pop %%r14;
            pop %%r13;
            pop %%r12;
            pop %%r11;
            pop %%r10;
            pop %%r9;
            pop %%r8;
            pop %%rbp;
            pop %%rdi;
            pop %%rsi;
            pop %%rdx;
            pop %%rcx;
            pop %%rbx
        )" 
            :
            : "m"(frameAddr), "m"(targetPc), "m"(targetSp)
            :);
        if (prevIntrs)
            IntrsOn();

        exitInfo.type = static_cast<HwUserExitType>(context->frame->type);
        //TODO: process exit info

        return exitInfo;
    }
}
