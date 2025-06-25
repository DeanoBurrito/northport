#pragma once

#include <Types.h>
#include <Compiler.h>

namespace sl
{
    template<typename AccessType>
    AccessType MmioRead(uintptr_t addr)
    {
        AccessType out;
#ifdef __x86_64__
        if constexpr (sizeof(AccessType) == 1)
            asm("movb (%1), %0" : "=a"(out) : "r"(addr));
        else if constexpr (sizeof(AccessType) == 2)
            asm("movw (%1), %0" : "=a"(out) : "r"(addr));
        else if constexpr (sizeof(AccessType) == 4)
            asm("movl (%1), %0" : "=a"(out) : "r"(addr));
        else if constexpr (sizeof(AccessType) == 8)
            asm("movq (%1), %0" : "=a"(out) : "r"(addr));
        else
            SL_UNREACHABLE();
#else
    #error "Unsupported architecture"
#endif
        return out;
    }

    inline uint8_t MmioRead8(uintptr_t addr) { return MmioRead<uint8_t>(addr); }
    inline uint16_t MmioRead16(uintptr_t addr) { return MmioRead<uint16_t>(addr); }
    inline uint32_t MmioRead32(uintptr_t addr) { return MmioRead<uint32_t>(addr); }
    inline uint64_t MmioRead64(uintptr_t addr) { return MmioRead<uint64_t>(addr); }

    template<typename AccessType>
    void MmioWrite(uintptr_t addr, AccessType value)
    {
#ifdef __x86_64__
        if constexpr (sizeof(AccessType) == 1)
            asm("movb %0, (%1)" :: "a"(value), "r"(addr));
        else if constexpr (sizeof(AccessType) == 2)
            asm("movw %0, (%1)" :: "a"(value), "r"(addr));
        else if constexpr (sizeof(AccessType) == 4)
            asm("movl %0, (%1)" :: "a"(value), "r"(addr));
        else if constexpr (sizeof(AccessType) == 8)
            asm("movq %0, (%1)" :: "a"(value), "r"(addr));
        else
            SL_UNREACHABLE();
#else
    #error "Unsupported architecture"
#endif
    }

    inline void MmioWrite8(uintptr_t addr, uint8_t what) { MmioWrite<uint8_t>(addr, what); }
    inline void MmioWrite16(uintptr_t addr, uint16_t what) { MmioWrite<uint16_t>(addr, what); }
    inline void MmioWrite32(uintptr_t addr, uint32_t what) { MmioWrite<uint32_t>(addr, what); }
    inline void MmioWrite64(uintptr_t addr, uint64_t what) { MmioWrite<uint64_t>(addr, what); }

    template<typename AccessType>
    class MmioRegister
    {
    private:
        uintptr_t base;

    public:
        constexpr MmioRegister() : base(0) {}
        constexpr MmioRegister(void* base) : base(reinterpret_cast<uintptr_t>(base)) {}
        constexpr MmioRegister(uintptr_t base) : base(base) {}

        inline AccessType Read() { return MmioRead<AccessType>(base); }
        inline void Write(AccessType what) { MmioWrite<AccessType>(base, what); }
    };

    template<typename RegEnum, typename RegType>
    class MmioRegisters
    {
    private:
        uintptr_t base;

    public:
        constexpr MmioRegisters() : base(0) {}
        constexpr MmioRegisters(void* base) : base(reinterpret_cast<uintptr_t>(base)) {}
        constexpr MmioRegisters(uintptr_t base) : base(base) {}

        inline void* BasePointer() const
        {
            return reinterpret_cast<void*>(base);
        }

        inline uintptr_t BaseAddress() const
        {
            return base;
        }

        inline RegType Read(RegEnum which)
        {
            const uintptr_t addr = base + static_cast<uintptr_t>(which);
            return MmioRead<RegType>(addr);
        }

        inline void Write(RegEnum which, RegType what)
        {
            const uintptr_t addr = base + static_cast<uintptr_t>(which);
            MmioWrite<RegType>(addr, what);
        }
    };
}
