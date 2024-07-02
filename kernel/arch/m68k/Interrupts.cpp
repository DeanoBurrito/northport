#include <arch/m68k/Interrupts.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <Locks.h>

namespace Npk
{
    sl::SpinLock vectorTableLock;
    uint32_t* vectorTable = nullptr;

    void TrapEntry() asm("TrapEntry"); //defined in Trap.S

    void LoadVectorTable()
    {
        vectorTableLock.Lock();
        if (vectorTable == nullptr)
        {
            vectorTable = new uint32_t[IntVectorAllocLimit];
            for (size_t i = 0; i < IntVectorAllocLimit; i++)
                vectorTable[i] = reinterpret_cast<uintptr_t>(&TrapEntry);

            Log("Vector table allocated at %p", LogLevel::Verbose, vectorTable);
        }
        vectorTableLock.Unlock();

        asm volatile("movec %0, %%vbr" :: "d"(vectorTable));
    }
}

extern "C"
{
    using namespace Npk;

    void TrapDispatch(TrapFrame* frame)
    {
        ASSERT_UNREACHABLE();
    }
}
