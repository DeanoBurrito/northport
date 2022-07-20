#pragma once

#include <stdint.h>
#include <NativePtr.h>
#include <Maths.h>

#define PAGE_FRAME_SIZE 0x1000
#define AP_BOOTSTRAP_BASE 0x44000
#define INTERRUPT_VECTORS_BASE 0xFFFF'FFFF'FF00'0000
#define HHDM_LIMIT (512 * GB)
#define SCHEDULER_TICK_MS 10

#define PORT_DEBUGCON 0xE9
#define PORT_PS2_DATA 0x60
#define PORT_PS2_COMMAND_STATUS 0x64
//we only really care about channel 0 of the PIT, hence why i'm just calling it data
#define PORT_PIT_DATA 0x40
#define PORT_PIT_COMMAND 0x43
#define PORT_PCI_CONFIG_ADDRESS 0xCF8
#define PORT_PCI_CONFIG_DATA 0xCFC

#define INT_VECTOR_SPURIOUS 0xFF
#define INT_VECTOR_IGNORE 0xFF
#define INT_VECTOR_PANIC 0xFE
#define INT_VECTOR_PS2KEYBOARD 0x21
#define INT_VECTOR_SCHEDULER_TICK 0x22
#define INT_VECTOR_PIT_TICK 0x23
#define INT_VECTOR_SYSCALL 0x24
#define INT_VECTOR_PS2MOUSE 0x2C

#define ALLOC_INT_VECTOR_BASE 0x30
#define ALLOC_INT_VECTOR_COUNT 0x60

#define MSR_IA32_EFER 0xC0000080
#define MSR_APIC_BASE 0x1B
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_GS_KERNEL_BASE 0xC0000102
#define MSR_LSTAR 0xC0000082
#define MSR_SFMASK 0xC0000084

#define GDT_ENTRY_RING_0_CODE 0x8
#define GDT_ENTRY_RING_0_DATA 0x10
#define GDT_ENTRY_RING_3_CODE 0x18
#define GDT_ENTRY_RING_3_DATA 0x20
#define GDT_ENTRY_TSS 0x28

#define PORT_CMOS_ADDRESS 0x70
#define PORT_CMOS_DATA 0x71

namespace Kernel
{
    [[gnu::always_inline]] inline
    uint64_t ReadCR0()
    {
        uint64_t value;
        asm volatile("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    [[gnu::always_inline]] inline
    void WriteCR0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    [[gnu::always_inline]] inline
    uint64_t ReadCR2()
    {
        uint64_t value;
        asm volatile("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    [[gnu::always_inline]] inline
    uint64_t ReadCR3()
    {
        uint64_t value;
        asm volatile("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    [[gnu::always_inline]] inline
    void WriteCR3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value));
    }

    [[gnu::always_inline]] inline
    uint64_t ReadCR4()
    {
        uint64_t value;
        asm volatile("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    [[gnu::always_inline]] inline
    void WriteCR4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }

    void PortWrite8(uint16_t port, uint8_t data);
    void PortWrite16(uint16_t port, uint16_t data);
    void PortWrite32(uint16_t port, uint32_t data);
    uint8_t PortRead8(uint16_t port);
    uint16_t PortRead16(uint16_t port);
    uint32_t PortRead32(uint16_t port);

    void WriteMsr(uint32_t address, uint64_t data);
    uint64_t ReadMsr(uint32_t address);

    enum CoreLocalIndices : size_t
    {
        LAPIC,
        TSS,
        CurrentThread,
        
        EnumCount,
    };

    struct CoreLocalStorage
    {
        uint64_t selfAddress; //you're welcome, future me
        uint64_t id;
        uint64_t acpiProcessorId;
        sl::NativePtr ptrs[CoreLocalIndices::EnumCount];
    };

    [[gnu::always_inline]] inline
    CoreLocalStorage* CoreLocal()
    {
        uint64_t addr = 0;
        asm("mov %%gs:0, %0" : "=r"(addr));
        return reinterpret_cast<CoreLocalStorage*>(addr);
    }

    struct [[gnu::packed]] StoredRegisters
    {
        //zero for local stack, non-zero for foreign stack.
        uint64_t stackType;
        
        uint64_t r15;
        uint64_t r14;
        uint64_t r13;
        uint64_t r12;
        uint64_t r11;
        uint64_t r10;
        uint64_t r9;
        uint64_t r8;
        uint64_t rbp;
        uint64_t rsp; //just a dummy value so its an even 16 regs - use iret_rsp for stack access
        uint64_t rdi;
        uint64_t rsi;
        uint64_t rdx;
        uint64_t rcx;
        uint64_t rbx;
        uint64_t rax;

        uint64_t vectorNumber;
        uint64_t errorCode;

        uint64_t iret_rip;
        uint64_t iret_cs;
        uint64_t iret_flags;
        uint64_t iret_rsp;
        uint64_t iret_ss;
    };
}
