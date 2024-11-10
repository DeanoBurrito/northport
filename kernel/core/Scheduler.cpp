#include <core/Scheduler.h>
#include <core/Log.h>
#include <core/Config.h>
#include <core/WiredHeap.h>
#include <core/Pmm.h>
#include <Entry.h>
#include <arch/Misc.h>
#include <Maths.h>

namespace Npk::Core
{
    constexpr size_t DefaultPriorityCount = 16;
    constexpr size_t MaxPrioritiesCount = 128;
    constexpr size_t IdleThreadStackPages = 8;

    SchedulerObj* Scheduler::PopThread()
    {
        sl::ScopedLock scopeLock(threadsLock);
        for (size_t i = liveThreads.Size(); i != 0; i--)
        {
            auto& queue = liveThreads[i - 1];
            if (queue.Empty())
                continue;
            return queue.PopFront();
        }

        return &idleThread;
    }

    void Scheduler::PushThread(SchedulerObj* obj)
    {
        sl::ScopedLock scopeLock(threadsLock);
        liveThreads[obj->priority].PushBack(obj);
    }

    Scheduler* Scheduler::Local()
    {
        return static_cast<Scheduler*>(GetLocalPtr(SubsysPtr::Scheduler));
    }

    static void IdleThreadMain(void* arg)
    {
        (void)arg;
        Halt();
    }

    void Scheduler::Init(sl::Span<uint8_t> idleStack)
    {
        size_t priorities = GetConfigNumber("kernel.scheduler.priorities", DefaultPriorityCount);
        priorities = sl::Clamp<size_t>(priorities, 1, MaxPrioritiesCount);

        auto queues = static_cast<SchedulerQueue*>(WiredAlloc(priorities * sizeof(SchedulerQueue)));
        ASSERT_(queues != nullptr);
        for (size_t i = 0; i < priorities; i++)
            new(queues + i) SchedulerQueue{};

        liveThreads = { queues, priorities };

        idleThread.active = false;
        idleThread.priority = priorities - 1;
        idleThread.extendedState = nullptr;
        idleThread.scheduler = this;
        idleThread.frame = reinterpret_cast<TrapFrame*>(idleStack.Begin());

        InitTrapFrame(idleThread.frame, reinterpret_cast<uintptr_t>(idleStack.End()), reinterpret_cast<uintptr_t>(IdleThreadMain), false);
        Log("Local scheduler init: %zu priorities, idleEntry=%p", LogLevel::Info, priorities, IdleThreadMain);
    }

    static void PrepareInitialThread(TrapFrame* next, void* arg)
    {
        (void)next;

        SchedulerObj* obj = static_cast<SchedulerObj*>(arg);
        SetLocalPtr(SubsysPtr::Thread, obj);
    }

    [[noreturn]]
    void Scheduler::Kickstart()
    {
        DisableInterrupts();

        auto next = PopThread();
        ASSERT_(next != nullptr);
        SwitchFrame(nullptr, PrepareInitialThread, next->frame, next);
        ASSERT_UNREACHABLE();
    }

    size_t Scheduler::DefaultPriority() const
    {
        return liveThreads.Size() / 2;
    }

    void Scheduler::Enqueue(SchedulerObj* obj, size_t priority)
    {
        VALIDATE_(obj != nullptr, );
        priority = sl::Min(priority, liveThreads.Size() - 1);

        sl::ScopedLock objLock(obj->lock);
        obj->scheduler = this;
        obj->priority = priority;
        obj->active = true;

        PushThread(obj);
    }

    void Scheduler::Dequeue(SchedulerObj* obj)
    {
        VALIDATE_(obj != nullptr, );

        sl::ScopedLock objLock(obj->lock);
        obj->scheduler = nullptr;
        obj->active = false;

        sl::ScopedLock scopeLock(threadsLock);
        liveThreads[obj->priority].Remove(obj);
    }

    void Scheduler::Yield()
    {
        const auto prevRl = RaiseRunLevel(RunLevel::Dpc);

        auto currThread = static_cast<SchedulerObj*>(GetLocalPtr(SubsysPtr::Thread));
        if (currThread->active)
            PushThread(currThread);

        auto nextThread = PopThread();
        SetLocalPtr(SubsysPtr::Thread, nextThread);
        SwitchFrame(&currThread->frame, nullptr, nextThread->frame, nullptr);

        LowerRunLevel(prevRl);
    }
}
