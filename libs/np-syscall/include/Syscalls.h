#pragma once

#include <NativePtr.h>
#include <Optional.h>

namespace np::Syscall
{   
    struct SyscallData
    { 
        uint64_t id, arg0, arg1, arg2, arg3; 

        SyscallData() = default;
        constexpr SyscallData(uint64_t id, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
        : id(id), arg0(arg0), arg1(arg1), arg2(arg2), arg3(arg3)
        {}
    };

    [[gnu::always_inline]]
    inline void DoSyscall(SyscallData* data)
    {
        asm volatile("\
            mov 0x00(%0), %%rax; \
            mov 0x08(%0), %%rdi; \
            mov 0x10(%0), %%rsi; \
            mov 0x18(%0), %%rdx; \
            mov 0x20(%0), %%rcx; \
            int $0x24; \
            mov %%rax, 0x00(%0); \
            mov %%rdi, 0x08(%0); \
            mov %%rsi, 0x10(%0); \
            mov %%rdx, 0x18(%0); \
            mov %%rcx, 0x20(%0); \
            " 
            : "=r"(data)
            : "r"(data)
            : "rax", "rdi", "rsi", "rdx", "rcx", "memory"
            );
    }
    
    bool SyscallLoopbackSuccess();
    
    struct PrimaryFramebufferData
    {
        sl::NativePtr baseAddress;
        NativeUInt width;
        NativeUInt height;
        NativeUInt stride;
        NativeUInt bpp;

        union
        {
            struct
            {
                uint8_t redOffset;
                uint8_t greenOffset;
                uint8_t blueOffset;
                uint8_t reserved1;
                uint8_t redMask;
                uint8_t greenMask;
                uint8_t blueMask;
                uint8_t reserved0;
            };
            uint64_t raw;
        } format;
    };

    sl::Opt<PrimaryFramebufferData> GetPrimaryFramebuffer();
}
