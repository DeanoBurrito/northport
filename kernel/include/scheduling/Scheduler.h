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
        bool suspended;
        size_t currentId;
        sl::Vector<Thread*> threads;
        sl::UIdAllocator idGen;
        
    public:
        static Scheduler* Local();

        void Init();
        StoredRegisters* SelectNextThread(StoredRegisters* currentRegs); //usually called from inside an interrupt handler
        [[noreturn]]
        void Yield();
        //causes the scheduler to immediately return from SelectNextThread, locking the current thread.
        void Suspend(bool suspendSelection);

        Thread* CreateThread(sl::NativePtr entryAddr, bool userspace);
        void RemoveThread(size_t id);
        Thread* GetCurrentThread();
    };
}
