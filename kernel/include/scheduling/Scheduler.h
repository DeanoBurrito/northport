#pragma once

#include <scheduling/Thread.h>
#include <containers/Vector.h>
#include <IdAllocator.h>

#define SCHEDULER_QUANTUM_MS 10

namespace Kernel::Scheduling
{
    class Scheduler
    {
    private:
        char lock;
        size_t currentId;
        sl::Vector<Thread*> threads;
        sl::UIdAllocator idGen;
        
    public:
        static Scheduler* Local();

        void Init();
        StoredRegisters* SelectNextThread(StoredRegisters* currentRegs); //usually called from inside an interrupt handler
        [[noreturn]]
        void Yield();

        Thread* CreateThread(sl::NativePtr entryAddr, bool userspace);
        void RemoveThread(size_t id);
        Thread* GetCurrentThread();
    };
}
