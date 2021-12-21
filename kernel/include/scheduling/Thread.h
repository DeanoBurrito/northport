#pragma once

#include <Platform.h>
#include <NativePtr.h>
#include <memory/Paging.h>

namespace Kernel::Scheduling
{
    class Scheduler;
    
    class Thread
    {
    friend Scheduler;
    private:
        size_t threadId;
        StoredRegisters* regs;
        
        Thread() = default;
        void Cleanup();

    public:
        static Thread* Current();

        void Start(sl::NativePtr arg);
        void Exit();
        size_t GetId() const;
    };
}
