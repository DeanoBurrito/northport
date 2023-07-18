#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk
{
    struct CoreConfig
    {
        uint64_t xSaveBitmap;
        size_t xSaveBufferSize;
        uint8_t* featureBitmap;
    };
    
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

    constexpr inline size_t PageSize = 0x1000;
    constexpr inline size_t IntVectorAllocBase = 0x30;
    constexpr inline size_t IntVectorAllocLimit = 0xFD;
    constexpr inline size_t IntVectorIpi = 0xFE;
    constexpr inline size_t IntVectorCount = 256;

    constexpr inline uint32_t MsrEfer = 0xC0000080;
    constexpr inline uint32_t MsrApicBase = 0x1B;
    constexpr inline uint32_t MsrGsBase = 0xC0000101;
    constexpr inline uint32_t MsrKernelGsBase = 0xC0000102;
    constexpr inline uint32_t MsrTsc = 0x10;
    constexpr inline uint32_t MsrTscDeadline = 0x6E0;

    constexpr inline uint16_t PortDebugcon = 0xE9;
    constexpr inline uint16_t PortPitCmd = 0x43;
    constexpr inline uint16_t PortPitData = 0x40;
    constexpr inline uint16_t PortSerial = 0x3F8;

    [[gnu::always_inline]]
    inline void Wfi()
    { 
        asm("hlt"); 
    }

    [[gnu::always_inline]]
    inline bool InterruptsEnabled()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0" : "=rm"(flags));
        return flags & (1 << 9);
    }

    [[gnu::always_inline]]
    inline void EnableInterrupts()
    {
        asm("sti" ::: "cc");
    }

    [[gnu::always_inline]]
    inline void DisableInterrupts()
    {
        asm("cli" ::: "cc");
    }

    [[gnu::always_inline]]
    inline void AllowSumac()
    {
        asm("stac" ::: "cc");
    }

    [[gnu::always_inline]]
    inline void BlockSumac()
    {
        asm("clac" ::: "cc");
    }

    [[gnu::always_inline]]
    inline uintptr_t MsiAddress(size_t core, size_t vector)
    {
        (void)vector;
        return ((core & 0xFF) << 12) | 0xFEE0'0000;
    }

    [[gnu::always_inline]]
    inline uintptr_t MsiData(size_t core, size_t vector)
    {
        (void)core;
        return vector & 0xFF;
    }

    [[gnu::always_inline]]
    inline void MsiExtract(uintptr_t addr, uintptr_t data, size_t& core, size_t& vector)
    {
        core = (addr >> 12) & 0xFF;
        vector = data & 0xFF;
    }

    [[gnu::always_inline]]
    inline uint8_t In8(uint16_t port)
    { 
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline uint16_t In16(uint16_t port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline uint32_t In32(uint16_t port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline void Out8(uint16_t port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    [[gnu::always_inline]]
    inline void Out16(uint16_t port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    [[gnu::always_inline]]
    inline void Out32(uint16_t port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    [[gnu::always_inline]]
    inline uint64_t ReadCr0()
    { 
        uint64_t value;
        asm("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    [[gnu::always_inline]]
    inline void WriteCr0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    [[gnu::always_inline]]
    inline uint64_t ReadCr2()
    { 
        uint64_t value;
        asm("mov %%cr2, %0" : "=r"(value) :: "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline uint64_t ReadCr3()
    { 
        uint64_t value;
        asm("mov %%cr3, %0" : "=r"(value) :: "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline void WriteCr3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value));
    }

    [[gnu::always_inline]]
    inline uint64_t ReadCr4()
    { 
        uint64_t value;
        asm("mov %%cr4, %0" : "=r"(value) :: "memory");
        return value;
    }

    [[gnu::always_inline]]
    inline void WriteCr4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }

    [[gnu::always_inline]]
    inline uint64_t ReadMsr(uint32_t addr)
    {
        uint32_t high, low;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(addr) : "memory");
        return ((uint64_t)high << 32) | low;
    }

    [[gnu::always_inline]]
    inline void WriteMsr(uint32_t addr, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"(data & 0xFFFF'FFFF), "d"(data >> 32), "c"(addr));
    }

    [[gnu::always_inline]]
    inline bool IsBsp()
    {
        return (ReadMsr(MsrApicBase) >> 8) & 1;
    }

    struct CoreLocalInfo;
    
    [[gnu::always_inline]]
    inline CoreLocalInfo& CoreLocal()
    {
        return *reinterpret_cast<CoreLocalInfo*>(ReadMsr(MsrGsBase));
    }

    [[gnu::always_inline]]
    inline bool CoreLocalAvailable()
    {
        return ReadMsr(MsrGsBase) != 0;
    }
}
