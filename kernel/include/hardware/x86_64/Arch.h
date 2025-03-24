#pragma once

#include <hardware/Arch.h>

#define GS_RELATIVE_READ(offset, value) do { asm("mov %%gs:" #offset ", %0" : "=r"(value) :: "memory"); } while(false)
#define GS_RELATIVE_READ_(offset, value) do { asm("mov %%gs:%c1, %0" : "=r"(value) : "i"(offset): "memory"); } while(false)
#define GS_RELATIVE_WRITE(offset, value) do { asm("mov %0, %%gs:" #offset :: "r"(value) : "memory"); } while(false)
#define GS_RELATIVE_WRITE_(offset, value) do { asm("mov %0, %%gs:%c1" :: "r"(value), "i"(offset) : "memory"); } while(false)

namespace Npk
{
    enum class Msr : uint32_t
    {
        ApicBase = 0x1B,
        Tsc = 0x10,
        TscDeadline = 0x6E0,
        Efer = 0xC0000080,
        GsBase = 0xC0000101,
        KernelGsBase = 0xC0000102,
        PvSystemTime = 0x4B564D01,
        PvWallClock = 0x4B564D00,
    };

    constexpr inline uint32_t MsrKernelGsBase = 0xC0000102;
    constexpr inline uint32_t MsrTsc = 0x10;
    constexpr inline uint32_t MsrTscDeadline = 0x6E0;
    constexpr inline uint32_t MsrPvSystemTime = 0x4B564D01;
    constexpr inline uint32_t MsrPvWallClock = 0x4B564D00;

    constexpr inline uint16_t PortDebugcon = 0xE9;
    constexpr inline uint16_t PortPitCmd = 0x43;
    constexpr inline uint16_t PortPitData = 0x40;

    constexpr uint16_t SelectorKernelCode = 0x08 | 0;
    constexpr uint16_t SelectorKernelData = 0x10 | 0;
    constexpr uint16_t SelectorUserData   = 0x18 | 3;
    constexpr uint16_t SelectorUserCode   = 0x20 | 3;
    constexpr uint16_t SelectorTss        = 0x28;

    constexpr uint8_t IntrVectorTimer = 0xFD;
    constexpr uint8_t IntrVectorIpi = 0xFE;
    constexpr uint8_t IntrVectorSpurious = 0xFF;

    struct CoreLocalBlock
    {
        size_t id;
        RunLevel rl;
        Core::DpcQueue dpcs;
        Core::ApcQueue apcs;
        uint64_t xsaveBitmap;
        size_t xsaveSize;
        MemoryDomain* memDom;
        void* subsysPtrs[static_cast<size_t>(SubsysPtr::Count)];
    };

    SL_ALWAYS_INLINE
    size_t ArchGetPerCpuSize()
    {
        return sizeof(CoreLocalBlock);
    }

    SL_ALWAYS_INLINE
    bool IntrsEnabled()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0" : "=rm"(flags) :: "memory");
        return flags & (1 << 9);
    }

    SL_ALWAYS_INLINE
    bool DisableIntrs()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0; cli" : "=rm"(flags) :: "memory", "cc");
        return flags & (1 << 9);
    }

    SL_ALWAYS_INLINE
    bool EnableIntrs()
    {
        uint64_t flags;
        asm volatile("pushf; pop %0; sti" : "=rm"(flags) :: "memory", "cc");
        return flags & (1 << 9);
    }

    SL_ALWAYS_INLINE
    void Wfi()
    {
        asm("hlt");
    }

    SL_ALWAYS_INLINE
    uint64_t ReadMsr(Msr which)
    {
        uint32_t high, low;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(static_cast<uint32_t>(which)) : "memory");
        return ((uint64_t)high << 32) | low;
    }

    SL_ALWAYS_INLINE
    void WriteMsr(Msr which, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"(data & 0xFFFF'FFFF), "d"(data >> 32), "c"(static_cast<uint32_t>(which)));
    }

    SL_ALWAYS_INLINE
    uint8_t In8(uint16_t port)
    { 
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    uint16_t In16(uint16_t port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    uint32_t In32(uint16_t port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    SL_ALWAYS_INLINE
    void Out8(uint16_t port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
    }

    SL_ALWAYS_INLINE
    void Out16(uint16_t port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port));
    }

    SL_ALWAYS_INLINE
    void Out32(uint16_t port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port));
    }

    SL_ALWAYS_INLINE
    uint64_t ReadCr0()
    { 
        uint64_t value;
        asm("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    SL_ALWAYS_INLINE
    void WriteCr0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    SL_ALWAYS_INLINE
    uint64_t ReadCr2()
    { 
        uint64_t value;
        asm("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    SL_ALWAYS_INLINE
    uint64_t ReadCr3()
    { 
        uint64_t value;
        asm("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    SL_ALWAYS_INLINE
    void WriteCr3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value) : "memory");
    }

    SL_ALWAYS_INLINE
    uint64_t ReadCr4()
    { 
        uint64_t value;
        asm("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    SL_ALWAYS_INLINE
    void WriteCr4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }

    SL_ALWAYS_INLINE
    void WriteCr8(uint64_t value)
    {
        asm volatile("mov %0, %%cr8" :: "r"(value));
    }

    SL_ALWAYS_INLINE
    uint64_t ReadTsc()
    {
        uint64_t low;
        uint64_t high;
        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");
        return low | (high << 32);
    }

    SL_ALWAYS_INLINE
    bool IsBsp()
    {
        return (ReadMsr(Msr::ApicBase) >> 8) & 1;
    }

    SL_ALWAYS_INLINE
    bool CoreLocalAvailable()
    {
        return ReadMsr(Msr::GsBase) != 0;
    }

    SL_ALWAYS_INLINE
    size_t CoreId()
    {
        size_t id;
        GS_RELATIVE_READ(0x0, id);
        return id;
    }
    static_assert(offsetof(CoreLocalBlock, id) == 0x0);

    SL_ALWAYS_INLINE
    RunLevel ExchangeRunLevel(RunLevel rl)
    {
        asm("xchg %0, %%gs:0x8" : "+r"(rl) :: "memory");
        return static_cast<RunLevel>(rl);
    }
    static_assert(offsetof(CoreLocalBlock, rl) == 0x8);

    SL_ALWAYS_INLINE
    RunLevel CurrentRunLevel()
    {
        unsigned rl;
        GS_RELATIVE_READ(0x8, rl);
        return static_cast<RunLevel>(rl);
    }
    static_assert(offsetof(CoreLocalBlock, rl) == 0x8);

    SL_ALWAYS_INLINE
    Core::DpcQueue* LocalDpcs()
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(Msr::GsBase));
        return &clb->dpcs;
    }

    SL_ALWAYS_INLINE
    Core::ApcQueue* LocalApcs()
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(Msr::GsBase));
        return &clb->apcs;
    }

    SL_ALWAYS_INLINE
    MemoryDomain& LocalDomain()
    {
        MemoryDomain* ptr;
        GS_RELATIVE_READ_(0x78, ptr);
        return *ptr;
    }
    static_assert(offsetof(CoreLocalBlock, memDom) == 0x78);

#define GetLocalPtr(which) \
    ({ \
        constexpr size_t offset = static_cast<size_t>(which) * sizeof(void*) + offsetof(CoreLocalBlock, subsysPtrs); \
        void* ptr; \
        GS_RELATIVE_READ_(offset, ptr); \
        ptr; \
     })

#define SetLocalPtr(which, data) \
    ({ \
        constexpr size_t offset = static_cast<size_t>(which) * sizeof(void*) + offsetof(CoreLocalBlock, subsysPtrs); \
        GS_RELATIVE_WRITE_(offset, data); \
     })


    SL_ALWAYS_INLINE
    size_t PfnShift()
    {
        return 12;
    }

    SL_ALWAYS_INLINE
    size_t KernelStackSize()
    {
        return 8 << 12;
    }
}
