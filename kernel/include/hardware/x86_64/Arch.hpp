#pragma once

#include <hardware/Arch.hpp>
#include <hardware/x86_64/Msr.hpp>

namespace Npk
{
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
        void* syscallStack;
    };
    static_assert(offsetof(CoreLocalHeader, syscallStack) == 24);

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
    DebugStatus ArchCallDebugger(DebugEventType type, void* data)
    {
        DebugStatus ret;
        asm("int $0xFB; mov %%eax, %0" : "=r"(ret) : "D"(type), "S"(data) : "memory", "rax");
        return ret;
    }
    static_assert(DebugEventVector == 0xFB);

#define READ_CR(num) ({ uint64_t val; asm("mov %%cr" #num ", %0" : "=r"(val)); val; })
#define WRITE_CR(num, val) do { asm("mov %0, %%cr" #num :: "r"(val) : "memory"); } while (false)
}
