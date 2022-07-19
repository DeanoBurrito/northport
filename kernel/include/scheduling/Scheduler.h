#pragma once

#include <IdAllocator.h>
#include <containers/Vector.h>
#include <scheduling/Thread.h>
#include <scheduling/BuiltinThreads.h>

namespace Kernel
{
    struct [[gnu::packed]] StoredRegisters;
}

namespace Kernel::Scheduling
{
    class ThreadGroup;

    //This struct is accessed from the trap handler, so the order of these fields must be fixed.
    struct [[gnu::packed]] ForeignStack
    {
        NativeUInt magic;
        NativeUInt translationAddr;
        NativeUInt stackAddr;
    };

    struct ProcessorStatus
    {
        char lock;
        bool isPlaceholder = true;

        //temporary data store, see Scheduler::Tick()
        ForeignStack targetStack;

        Thread* idleThread;
        Thread* lastSelected;
        sl::Vector<Thread*> threads; //non-owning ptrs
    };

    class Scheduler
    {
    friend Thread;
    private:
        char globalLock;
        bool suspended;

        sl::UIdAllocator threadIdAlloc;
        sl::UIdAllocator groupIdAlloc;
        sl::Vector<ThreadGroup*> threadGroups;
        sl::Vector<Thread*> allThreads;
        sl::Vector<ProcessorStatus> processors;

        ThreadGroup* idleGroup;
        CleanupData cleanupData;

        void SpawnUtilityThreads();
        bool EnqueueOnCore(Thread* thread, size_t core);
        bool DequeueFromCore(Thread* thread, size_t core);

    public:
        static Scheduler* Global();

        void Init();
        void AddProcessor(size_t id);
        StoredRegisters* Tick(StoredRegisters* regs);
        void Yield();
        void YieldCore(size_t core);
        void Suspend(bool yes = true);

        ThreadGroup* CreateThreadGroup();
        void RemoveThreadGroup(size_t id);
        sl::Opt<ThreadGroup*> GetThreadGroup(size_t id) const;
        Thread* CreateThread(sl::NativePtr entryAddress, ThreadFlags flags, ThreadGroup* parent = nullptr, size_t coreIndex = (size_t)-1);
        void RemoveThread(size_t id);
        sl::Opt<Thread*> GetThread(size_t id) const;
    };
}
