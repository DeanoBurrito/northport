#pragma once

#include <Platform.h>
#include <NativePtr.h>
#include <memory/Paging.h>

namespace Kernel::Scheduling
{
    class Scheduler;

    enum class ThreadState
    {
        PendingStart,
        Running,
        Sleeping,
        PendingCleanup,
    };

    enum class ThreadFlags : uint32_t
    {
        None = 0,

        KernelMode = (1 << 0),
    };
    
    class Thread
    {
    friend Scheduler;
    private:
        size_t threadId;
        StoredRegisters* regs;
        ThreadFlags flags;
        ThreadState runState;
        
        Thread() = default;
        void Cleanup();

    public:
        static Thread* Current();

        void Start(sl::NativePtr arg);
        void Exit();
        size_t GetId() const;
        ThreadState GetState() const;
        ThreadFlags GetFlags() const;
    };
}
