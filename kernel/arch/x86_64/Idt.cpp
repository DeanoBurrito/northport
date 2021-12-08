#include <arch/x86_64/Idt.h>
#include <Memory.h>
#include <memory/Paging.h>

#include <Log.h>

namespace Kernel
{
    extern void* InterruptStub_Begin asm ("InterruptStub_Begin");
    extern void* InterruptStub_End asm ("InterruptStub_End");
    extern void* InterruptStub_PatchCall asm("InterruptStub_PatchCall");
    
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
        uint64_t config = (ist & 0b11) | ((type & 0b1111) << 8) | ((dpl & 0b11) << 13) | (1 << 15);
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

    [[gnu::used]]
    StoredRegisters* InterruptDispatch(StoredRegisters* regs)
    {
        //just return the existing registers
        Log("Hello from interrupt! :D", LogSeverity::Info);
        return regs;
    }

    void* CreateClonedEntry(uint8_t vectorNum, bool pushDummyErrorCode, uint64_t& latestPage, size_t& pageOffset)
    {
        /*
            This function is not for the fainthearted, you have been warned.

            In an attempt to not store a nearly identical stub for 256 interrupts, only to have them funnel to c++ code,
            I created this monster.
            It takes the output of IdtStub.s (marked by 2 labels InterruptStub_[Begin|End]), copies that to a new region of memory,
            we're allocating that ourselves (heap is not executable). It'll add opcodes to push the current interrupt vector before
            the pre-compiled stub, and optionally push a dummy error code of zero if needed.
            It'll also replace the 5 zero bytes with a call to InterruptDispatch, which actually handles the interrupts.
        */
        
        const size_t stubSourceSize = (uint64_t)&InterruptStub_End - (uint64_t)&InterruptStub_Begin;
        const size_t stubFullsize = stubSourceSize + (pushDummyErrorCode ? 4 : 2);
        const size_t patchCallOffset = (uint64_t)&InterruptStub_PatchCall - (uint64_t)&InterruptStub_Begin + (pushDummyErrorCode ? 4 : 2);
        
        //check we'll need to allocate another page or not
        uint64_t nextPage = latestPage;
        if (pageOffset + stubFullsize > PAGE_FRAME_SIZE)
        {
            using namespace Memory;
            nextPage = latestPage + PAGE_FRAME_SIZE;
            PageTableManager::Local()->MapMemory(nextPage, MemoryMapFlag::AllowExecute | MemoryMapFlag::AllowWrites);
        }

        uint8_t* handlerData = (uint8_t*)(latestPage + pageOffset);
        sl::NativePtr stubStackPtr = handlerData;

        //keep track of where we are in the latest page
        pageOffset += stubFullsize;
        if (pageOffset > PAGE_FRAME_SIZE)
            pageOffset -= PAGE_FRAME_SIZE;
        
        if (pushDummyErrorCode)
        {
            sl::StackPush<uint8_t, true>(stubStackPtr, 0x6A);
            sl::StackPush<uint8_t, true>(stubStackPtr, 0);
        }
        sl::StackPush<uint8_t, true>(stubStackPtr, 0x6A);
        sl::StackPush<uint8_t, true>(stubStackPtr, vectorNum);

        sl::memcopy(&InterruptStub_Begin, 0, handlerData, (pushDummyErrorCode ? 4 : 2), stubSourceSize);

        //now we have to patch the relative call instruction, use the offset from above
        sl::MemWrite<uint8_t>((uint64_t)handlerData + patchCallOffset, 0xE8); //CALL, taking imm32 (in 64bit mode). 
        const int32_t relativeOffset = (uint64_t)&InterruptDispatch - ((uint64_t)handlerData + patchCallOffset + 5);
        sl::MemWrite<uint32_t>((uint64_t)handlerData + patchCallOffset + 1, relativeOffset);

        latestPage = nextPage;
        return handlerData;
    }
    
    void SetupIDT()
    {
        defaultIDTR.limit = 0x0FFF;
        defaultIDTR.base = reinterpret_cast<uint64_t>(idtEntries);

        sl::memset(idtEntries, 0, sizeof(IdtEntry) * (HighestUserInterrupt + 1));

        uint64_t stubsBase = 0xffff'ffff'f000'0000;
        size_t pageOffset = 0;
        Memory::PageTableManager::Local()->MapMemory(stubsBase, Memory::MemoryMapFlag::AllowExecute | Memory::MemoryMapFlag::AllowWrites);

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
            
            void* entry = CreateClonedEntry(i, needsDummyEC, stubsBase, pageOffset);
            IdtEntry* idtEntry = defaultIDTR.GetEntry(i);
            idtEntry->SetAddress((uint64_t)entry);
            idtEntry->SetDetails(0, 0x8, IdtGateType::InterruptGate, 0);
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
