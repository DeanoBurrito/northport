#pragma once

#include <Hardware.hpp>

#define NPK_MAKE_HW_REG(name, type, index) \
    constexpr HwReg HwReg_##name = \
        (HwReg)(index + ((size_t)HwRegType::type << HwRegShift))

namespace Npk
{
    NPK_MAKE_HW_REG(rax, General, 0);
    NPK_MAKE_HW_REG(rbx, General, 1);
    NPK_MAKE_HW_REG(rcx, General, 2);
    NPK_MAKE_HW_REG(rdx, General, 3);
    NPK_MAKE_HW_REG(rdi, General, 4);
    NPK_MAKE_HW_REG(rsi, General, 5);
    NPK_MAKE_HW_REG(rbp, General, 6);
    NPK_MAKE_HW_REG(rsp, General, 7);
    NPK_MAKE_HW_REG(r8 , General, 8);
    NPK_MAKE_HW_REG(r9 , General, 9);
    NPK_MAKE_HW_REG(r10, General, 10);
    NPK_MAKE_HW_REG(r11, General, 11);
    NPK_MAKE_HW_REG(r12, General, 12);
    NPK_MAKE_HW_REG(r13, General, 13);
    NPK_MAKE_HW_REG(r14, General, 14);
    NPK_MAKE_HW_REG(r15, General, 15);
    NPK_MAKE_HW_REG(cs, System, 0);
    NPK_MAKE_HW_REG(ss, System, 1);
    NPK_MAKE_HW_REG(ds, System, 2);
    NPK_MAKE_HW_REG(es, System, 3);
    NPK_MAKE_HW_REG(fs, System, 4);
    NPK_MAKE_HW_REG(gs, System, 5);

    struct HwMap
    {
        Paddr ptRoot;
    };

    struct HwPte
    {
        uint64_t value;
    };

    struct UserFrame;

    struct HwUserContext
    {
        uint64_t resumePc;
        uint64_t resumeSp;
        UserFrame* frame;

        struct
        {
            bool logExit;
            uint8_t logSyscallLevel;
        } feats;
    };

    void CommonCpuSetup();

    SL_ALWAYS_INLINE
    size_t PfnShift()
    {
        return 12;
    }

    struct CoreLocalHeader
    {
        CpuId swId;
        uintptr_t selfAddr;
        ThreadContext* currThread;
        void* userExitStack;
        void (*ExceptRecoveryPc)(void* stack);
        void* exceptRecoveryStack;
    };
    static_assert(offsetof(CoreLocalHeader, userExitStack) == 24);
    static_assert(offsetof(CoreLocalHeader, ExceptRecoveryPc) == 32);
    static_assert(offsetof(CoreLocalHeader, exceptRecoveryStack) == 40);

    SL_ALWAYS_INLINE
    CpuId MyCoreId()
    {
        CpuId id;
        asm("mov %%gs:0, %0" : "=r"(id));
        return id;
    }
    static_assert(offsetof(CoreLocalHeader, swId) == 0);

    SL_ALWAYS_INLINE
    uintptr_t MyCpuLocals()
    {
        uintptr_t addr;
        asm("mov %%gs:8, %0" : "=r"(addr));
        return addr;
    }
    static_assert(offsetof(CoreLocalHeader, selfAddr) == 8);

    SL_ALWAYS_INLINE
    ThreadContext* GetCurrentThread()
    {
        ThreadContext* ptr;
        asm("mov %%gs:16, %0" : "=r"(ptr));
        return ptr;
    }
    static_assert(offsetof(CoreLocalHeader, currThread) == 16);

    SL_ALWAYS_INLINE
    void SetCurrentThread(ThreadContext* thread)
    {
        asm("mov %0, %%gs:16" :: "r"(thread));
    }
    static_assert(offsetof(CoreLocalHeader, currThread) == 16);

    SL_ALWAYS_INLINE
    void WaitForIntr()
    {
        asm("hlt");
    }

    SL_ALWAYS_INLINE
    bool IntrsExchange(bool on)
    {
        uint64_t flags;
        asm("pushf; pop %0" : "=g"(flags) :: "memory");

        if (on)
            asm("sti");
        else
            asm("cli");

        return flags & (1 << 9);
    }

    constexpr uint8_t DebugEventVector = 0xFB;

    SL_ALWAYS_INLINE
    NpkStatus HwCallDebugger(DebugEventType type, void* data)
    {
        NpkStatus ret;
        asm("int $0xFB; mov %%eax, %0" : "=r"(ret) : "D"(type), "S"(data) : "memory", "rax");
        return ret;
    }
    static_assert(DebugEventVector == 0xFB);
}

#define READ_CR(num) ({ uint64_t val; asm("mov %%cr" #num ", %0" : "=r"(val)); val; })
#define WRITE_CR(num, val) do { asm("mov %0, %%cr" #num :: "r"(val) : "memory"); } while (false)
#define READ_DR(num) ({ uint64_t val; asm("mov %%dr" #num", %0" : "=r"(val)); val; })
#define WRITE_DR(num, val) do { asm("mov %0, %%dr" #num :: "r"(val) : "memory"); } while (false)
#define READ_SR(id) ({ uint16_t val; asm("mov %%" #id ", %0" : "=r"(val)); val; })
#define WRITE_SR(id, val) do { asm("mov %0, %%" #id :: "r"(val) : "memory"); } while (false)
