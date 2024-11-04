#pragma once

#include <stddef.h>
#include <stdint.h>
#include <interfaces/intra/Compiler.h>
#include <core/RunLevels.h>

namespace Npk
{
    constexpr inline uint32_t MsrEfer = 0xC0000080;
    constexpr inline uint32_t MsrApicBase = 0x1B;
    constexpr inline uint32_t MsrGsBase = 0xC0000101;
    constexpr inline uint32_t MsrKernelGsBase = 0xC0000102;
    constexpr inline uint32_t MsrTsc = 0x10;
    constexpr inline uint32_t MsrTscDeadline = 0x6E0;

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

    class LocalApic;

    struct CoreLocalBlock
    {
        size_t id;
        RunLevel rl;
        Core::DpcQueue dpcs;
        Core::ApcQueue apcs;
        void* subsysPtrs[static_cast<size_t>(SubsysPtr::Count)];
    };

    ALWAYS_INLINE
    uint64_t ReadMsr(uint32_t addr)
    {
        uint32_t high, low;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(addr) : "memory");
        return ((uint64_t)high << 32) | low;
    }

    ALWAYS_INLINE
    void WriteMsr(uint32_t addr, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"(data & 0xFFFF'FFFF), "d"(data >> 32), "c"(addr));
    }

    ALWAYS_INLINE
    size_t CoreLocalId()
    {
        auto clb = reinterpret_cast<const CoreLocalBlock*>(ReadMsr(MsrGsBase)); //TODO: this is shit, reading an MSR everything - use __seg_gs stuff instead, or manually do gs-relative loads
        return clb->id;
    }

    ALWAYS_INLINE
    RunLevel CurrentRunLevel()
    {
        auto clb = reinterpret_cast<const CoreLocalBlock*>(ReadMsr(MsrGsBase));
        return clb->rl;
    }

    ALWAYS_INLINE
    void SetRunLevel(RunLevel rl)
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(MsrGsBase));
        clb->rl = rl;
    }

    ALWAYS_INLINE
    Core::DpcQueue* CoreLocalDpcs()
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(MsrGsBase));
        return &clb->dpcs;
    }

    ALWAYS_INLINE
    Core::ApcQueue* CoreLocalApcs()
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(MsrGsBase));
        return &clb->apcs;
    }

    ALWAYS_INLINE
    void* GetLocalPtr(SubsysPtr which)
    {
        auto clb = reinterpret_cast<const CoreLocalBlock*>(ReadMsr(MsrGsBase));
        return clb->subsysPtrs[static_cast<unsigned>(which)];
    }

    ALWAYS_INLINE
    void SetLocalPtr(SubsysPtr which, void* data)
    {
        auto clb = reinterpret_cast<CoreLocalBlock*>(ReadMsr(MsrGsBase));
        clb->subsysPtrs[static_cast<unsigned>(which)] = data;
    }

    ALWAYS_INLINE
    size_t PfnShift()
    {
        return 12;
    }

    ALWAYS_INLINE
    uint8_t In8(uint16_t port)
    { 
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    ALWAYS_INLINE
    uint16_t In16(uint16_t port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    ALWAYS_INLINE
    uint32_t In32(uint16_t port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        return value;
    }

    ALWAYS_INLINE
    void Out8(uint16_t port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    ALWAYS_INLINE
    void Out16(uint16_t port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    ALWAYS_INLINE
    void Out32(uint16_t port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port) : "memory");
    }

    ALWAYS_INLINE
    uint64_t ReadCr0()
    { 
        uint64_t value;
        asm("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    ALWAYS_INLINE
    void WriteCr0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    ALWAYS_INLINE
    uint64_t ReadCr2()
    { 
        uint64_t value;
        asm("mov %%cr2, %0" : "=r"(value) :: "memory");
        return value;
    }

    ALWAYS_INLINE
    uint64_t ReadCr3()
    { 
        uint64_t value;
        asm("mov %%cr3, %0" : "=r"(value) :: "memory");
        return value;
    }

    ALWAYS_INLINE
    void WriteCr3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value));
    }

    ALWAYS_INLINE
    uint64_t ReadCr4()
    { 
        uint64_t value;
        asm("mov %%cr4, %0" : "=r"(value) :: "memory");
        return value;
    }

    ALWAYS_INLINE
    void WriteCr4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }

    ALWAYS_INLINE
    void WriteCr8(uint64_t value)
    {
        asm volatile("mov %0, %%cr8" :: "r"(value));
    }

    ALWAYS_INLINE
    uint64_t ReadTsc()
    {
        uint64_t low;
        uint64_t high;
        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) :: "memory");
        return low | (high << 32);
    }

    ALWAYS_INLINE
    bool IsBsp()
    {
        return (ReadMsr(MsrApicBase) >> 8) & 1;
    }

    ALWAYS_INLINE
    bool CoreLocalAvailable()
    {
        return ReadMsr(MsrGsBase) != 0;
    }
}
