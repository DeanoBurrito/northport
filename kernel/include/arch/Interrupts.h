#pragma once

#include <arch/__Select.h>
#include <core/RunLevels.h>
#include <stdint.h>
#include <Optional.h>

namespace Npk
{
    struct MsiConfig
    {
        uintptr_t address;
        uintptr_t data;
    };

    struct TrapFrame;
    struct SyscallFrame;

    bool InterruptsEnabled();
    void EnableInterrupts();
    void DisableInterrupts();

    bool SendIpi(size_t dest);
    bool RoutePinInterrupt(size_t pin, size_t core, size_t vector);

    void Wfi();
    sl::Opt<MsiConfig> ConstructMsi(size_t core, size_t vector);
    bool DeconstructMsi(MsiConfig cfg, size_t& core, size_t& vector);

    size_t TrapFrameArgCount();
    void InitTrapFrame(TrapFrame* frame, uintptr_t stack, uintptr_t entry, bool user);
    void SetTrapFrameArg(TrapFrame* frame, size_t index, void* value);
    void* GetTrapFrameArg(TrapFrame* frame, size_t index);
    void SwitchFrame(TrapFrame** prev, TrapFrame* next) asm("SwitchFrame");

    size_t SyscallFrameArgCount();
    void SetSyscallArg(SyscallFrame* frame, size_t index, void* value);
    void* GetSyscallArg(SyscallFrame* frame, size_t index);

    [[noreturn]]
    inline void Halt()
    { 
        while (true) 
            Wfi(); 
        __builtin_unreachable();
    }
}

#ifdef NPK_ARCH_INCLUDE_INTERRUPTS
#include NPK_ARCH_INCLUDE_INTERRUPTS
#endif
