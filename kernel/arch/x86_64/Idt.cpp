#include <arch/x86_64/Idt.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel
{
    extern uint8_t TrapEntry[] asm ("TrapEntry");
    extern uint8_t TrapExit[] asm ("TrapExit");
    
    void IdtEntry::SetAddress(uint64_t where)
    {
        high |= (0xFFFF'FFFF'0000'0000 & where) >> 32;
        low |= (0xFFFF'0000 & where) << 32;
        low |= (0xFFFF & where);
    }

    uint64_t IdtEntry::GetAddress() const
    {
        uint64_t value = 0;
        value |= (high & 0xFFFF'FFFF) << 32;
        value |= (low & 0xFFFF'0000'0000'0000) >> 32;
        value |= (low & 0xFFFF);
        return value;
    }

    void IdtEntry::SetDetails(uint8_t ist, uint16_t codeSegment, IdtGateType type, uint8_t dpl)
    {
        //the last 1 << 15 is the present flag, and should always be set
        uint64_t config = (ist & 0b11) | ((type & 0b1110) << 8) | ((dpl & 0b11) << 13) | (1 << 15);
        low |= codeSegment << 16;

        //clear existing config (ensure zeros are still zero), then install our current
        low &= 0xFFFF'0000'FFFF'FFFF;
        low |= config << 32;
    }

    //statically allocate idt and idtr (they're trivially constructable, so they're created in .bss, not .data).
    [[gnu::aligned(0x8)]] 
    IdtEntry idtEntries[HighestUserInterrupt + 1];

    [[gnu::aligned(0x8)]] 
    IDTR defaultIDTR;

    IdtEntry* IDTR::GetEntry(size_t index)
    {
        return &idtEntries[index];
    }

    sl::NativePtr CreateIdtStub(uint8_t vector, bool needsDummyErrorCode, sl::NativePtr& codeStack)
    {
        /*
            For entry interrupt entry we need either 7 bytes (no EC) or 9 bytes (we need to push an EC).
            To keep things simple we'll round up to 16 bytes per entry stub, which nicely occupies a full 4K page.
        */

        constexpr size_t EntryAlignment = 0x10;

        const sl::NativePtr vectorEntry = codeStack;
        if (needsDummyErrorCode)
        {
            sl::StackPush<uint8_t, true>(codeStack, 0x6A); //0x6A = pushq
            sl::StackPush<uint8_t, true>(codeStack, 0);
        }
        sl::StackPush<uint8_t, true>(codeStack, 0x6A);
        sl::StackPush<uint8_t, true>(codeStack, vector);

        const int32_t relativeJumpOperand = (int32_t)(uint64_t)&TrapEntry - ((int32_t)codeStack.raw + 5);

        //jmp imm32 (1 byte opcode + 4 byte operand: signed 32bit offset)
        //we have to write the operand as 4 individual bytes, otherwise ubsan will freak out.
        sl::StackPush<uint8_t, true>(codeStack, 0xE9);
        sl::StackPush<uint8_t, true>(codeStack, (relativeJumpOperand >> 0) & 0xFF);
        sl::StackPush<uint8_t, true>(codeStack, (relativeJumpOperand >> 8) & 0xFF);
        sl::StackPush<uint8_t, true>(codeStack, (relativeJumpOperand >> 16) & 0xFF);
        sl::StackPush<uint8_t, true>(codeStack, (relativeJumpOperand >> 24) & 0xFF);

        //align address for next entry
        codeStack.raw = (codeStack.raw / EntryAlignment + 1) * EntryAlignment;
        return vectorEntry;
    }
    
    void SetupIDT()
    {
        defaultIDTR.limit = 0x0FFF;
        defaultIDTR.base = reinterpret_cast<uint64_t>(idtEntries);

        sl::memset(idtEntries, 0, sizeof(IdtEntry) * (HighestUserInterrupt + 1));
        Memory::PageTableManager::Current()->MapMemory(INTERRUPT_VECTORS_BASE, Memory::MemoryMapFlags::AllowExecute | Memory::MemoryMapFlags::AllowWrites);
        sl::NativePtr codeStack = INTERRUPT_VECTORS_BASE;

        for (size_t i = 0; i <= HighestUserInterrupt; i++)
        {
            bool needsDummyEC = true;
            switch (i) //maybe not the prettiest solution, but its efficient and it works
            {
                case DoubleFault:
                case InvalidTSS:
                case SegmentNotPresent:
                case StackSegmentFault:
                case GeneralProtectionFault:
                case PageFault:
                case AlignmentCheck:
                case ControlProtectionException:
                    needsDummyEC = false;
                    break;
            }

            bool userDpl = false;
            switch (i)
            {
                case INT_VECTOR_SYSCALL:
                    userDpl = true;
                    break;
            }
            
            const sl::NativePtr entryPoint = CreateIdtStub(i, needsDummyEC, codeStack);
            IdtEntry* idtEntry = defaultIDTR.GetEntry(i);
            idtEntry->SetAddress(entryPoint.raw);
            idtEntry->SetDetails(0, 0x8, IdtGateType::InterruptGate, userDpl ? 3 : 0);
        }

        Logf("Setup IDT for 0x%x entries (max platform supported).", LogSeverity::Verbose, HighestUserInterrupt);
    }

    [[gnu::naked]]
    void LoadIDT()
    {
        asm volatile("lidt 0(%0)" :: "r"(&defaultIDTR));
        asm volatile("ret");
    }
}
