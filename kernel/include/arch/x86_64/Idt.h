#pragma once

#include <stdint.h>
#include <stddef.h>
#include <NativePtr.h>

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
    
    sl::NativePtr CreateIdtStub(uint8_t vector, bool needsDummyErrorCode, sl::NativePtr& codeStack); 
    void SetupIDT();

    [[gnu::naked]]
    void LoadIDT();
}
