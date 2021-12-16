#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Kernel
{
    //just a fancy wrapper around the x86(_64) architectural exceptions
    enum NativeExceptions : uint8_t
    {
        //#DE - fault
        DivideError = 0,
        //#DB - fault/trap
        DebugException = 1,
        //(no mnemonic) - interrupt
        NonMaskableInterrupt = 2,
        //#BP - trap
        Breakpoint = 3,
        //#OF - trap
        Overflow = 4,
        //#BR - fault
        BoundRangeExceeded = 5,
        //#UD - fault
        InvalidOpcode = 6,
        //#NM - fault
        CoProcessorNotAvailable = 7,
        //#DF - abort, error code (always zero)
        DoubleFault = 8,
        //#TS - fault, error code
        InvalidTSS = 10,
        //#NP - fault, error code
        SegmentNotPresent = 11,
        //#SS - fault, error code
        StackSegmentFault = 12,
        //#GP - fault, error code
        GeneralProtectionFault = 13,
        //#PF - fault, error code (CR2 has linear address)
        PageFault = 14,
        //#MF - fault
        MathsFault = 16,
        //#AC - fault, error code (always zero)
        AlignmentCheck = 17,
        //#MC - abort
        MachineCheck = 18,
        //#XM - fault
        SimdException = 19,
        //#VE - fault
        VirtualizationException = 20,
        //#CP - fault
        ControlProtectionException = 21,

        HighestArchReserved = 31,
        LowestUserInterrupt = 32,
        HighestUserInterrupt = 255,
    };

    enum IdtGateType : uint8_t
    {
        TssAvailable = 0b1001,
        TssBusy = 0b1011,
        CallGate = 0b1100,
        InterruptGate = 0b1110,
        TrapGate = 0b1111,
    };
    
    struct [[gnu::packed]] IdtEntry
    {
        uint64_t low;
        uint64_t high;

        IdtEntry() = default;

        void SetAddress(uint64_t where);
        uint64_t GetAddress() const;
        void SetDetails(uint8_t ist, uint16_t codeSegment, IdtGateType type, uint8_t dpl);
    };

    struct [[gnu::packed]] IDTR
    {
        uint16_t limit;
        uint64_t base;

        IDTR() = default;

        IdtEntry* GetEntry(size_t index);
    };
    

    struct [[gnu::packed]] StoredRegisters
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

    extern "C"
    {
        //all interrupt stubs route here
        StoredRegisters* InterruptDispatch(StoredRegisters* regs);
    }

    [[gnu::used]]
    void* CreateClonedEntry(uint8_t vectorNum, bool pushDummyErrorCode, uint64_t& latestPage, size_t& pageOffset);
    void SetupIDT();

    [[gnu::naked]]
    void LoadIDT();
}
