#include <KernelThread.h>
#include <arch/Misc.h>
#include <arch/Interrupts.h>
#include <core/Log.h>
#include <services/Vmm.h>

namespace Npk
{
    struct KernelThread
    {
        Core::SchedulerObj schedObj;
        uintptr_t stackBase;
    };

    sl::Opt<Core::SchedulerObj*> CreateKernelThread(void (*entry)(void*), void* arg)
    {
        const auto stack = Services::VmAllocWired(nullptr, KernelStackSize(), 0, 
            VmViewFlag::Write | VmViewFlag::Private);
        VALIDATE_(stack.HasValue(), {});

        uintptr_t stackTop = reinterpret_cast<uintptr_t>(*stack) + KernelStackSize();
        stackTop = sl::AlignDown(stackTop - sizeof(KernelThread), alignof(KernelThread));
        KernelThread* meta = new(reinterpret_cast<void*>(stackTop)) KernelThread();

        meta->stackBase = reinterpret_cast<uintptr_t>(*stack);
        meta->schedObj.frame = InitTrapFrame(stackTop, reinterpret_cast<uintptr_t>(entry), false);
        SetTrapFrameArg(meta->schedObj.frame, 0, arg);

        return &meta->schedObj;
    }

    void DestroyKernelThread(Core::SchedulerObj* thread)
    {
        VALIDATE_(thread != nullptr, );

        KernelThread* meta = reinterpret_cast<KernelThread*>(thread);
        Services::VmUnwire(reinterpret_cast<void*>(meta->stackBase), KernelStackSize());
        Services::VmFree(reinterpret_cast<void*>(meta->stackBase));
    }
}
