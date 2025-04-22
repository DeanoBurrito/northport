#pragma once

#include <Types.h>
#include <Compiler.h>

namespace sl
{
    template <typename RegEnum, typename RegType>
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
            RegType out;

#ifdef __x86_64__
            if constexpr (sizeof(RegType) == 1)
                asm("movb (%1), %0" : "=r"(out) : "r"(addr));
            else if constexpr (sizeof(RegType) == 2)
                asm("movw (%1), %0" : "=r"(out) : "r"(addr));
            else if constexpr (sizeof(RegType) == 4)
                asm("movl (%1), %0" : "=r"(out) : "r"(addr));
            else if constexpr (sizeof(RegType) == 8)
                asm("movq (%1), %0" : "=r"(out) : "r"(addr));
            else
                SL_UNREACHABLE();
#else
    #error "Unsupported architecture"
#endif

            return out;
        }

        inline void Write(RegEnum which, RegType what)
        {
            const uintptr_t addr = base + static_cast<uintptr_t>(which);

#ifdef __x86_64__
            if constexpr (sizeof(RegType) == 1)
                asm("movb %0, (%1)" :: "r"(what), "r"(addr));
            else if constexpr (sizeof(RegType) == 2)
                asm("movw %0, (%1)" :: "r"(what), "r"(addr));
            else if constexpr (sizeof(RegType) == 4)
                asm("movl %0, (%1)" :: "r"(what), "r"(addr));
            else if constexpr (sizeof(RegType) == 8)
                asm("movq %0, (%1)" :: "r"(what), "r"(addr));
            else
                SL_UNREACHABLE();
#else
    #error "Unsupported architecture"
#endif
        }
    };
}
