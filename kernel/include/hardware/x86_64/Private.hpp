#pragma once

#include <hardware/Arch.hpp>

namespace Npk
{
    constexpr uint8_t Int3Opcode = 0xCC;

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

    struct SwitchFrame
    {
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rbx;
        uint64_t rbp;
        uint64_t r12;
        uint64_t r13;
        uint64_t r14;
        uint64_t r15;
        uint64_t flags;
    };

    bool CheckForDebugcon();
    bool CheckForCom1(bool debuggerOnly);
    void InitMachineChecking();

    void HandleDebugException(TrapFrame* frame, bool int3);
    void HandleMachineCheckException(TrapFrame* frame);
}
