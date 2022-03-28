#include <scheduling/Thread.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>

namespace Kernel::Scheduling
{
    Thread* Thread::Current()
    { return GetCoreLocal()->ptrs[CoreLocalIndices::CurrentThread].As<Thread>(); }

    size_t Thread::GetId() const
    { return id; }

    ThreadFlags Thread::GetFlags() const
    { return flags; }

    ThreadState Thread::GetState() const
    { return runState; }
    
    ThreadGroup* Thread::GetParent() const
    { return parent; }

    void Thread::Start(sl::NativePtr arg)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        //we'll need the physical address of the program's stack, and then access it through the hhdm to avoid swapping CR3.
        auto maybeStack = parent->VMM()->PageTables().GetPhysicalAddress(programStack);
        sl::NativePtr stackPhys = *maybeStack;
        stackPhys = EnsureHigherHalfAddr(maybeStack->ptr);
        stackPhys.As<StoredRegisters>()->rsi = arg.raw;
        runState = ThreadState::Running;
    }

    void Thread::Exit()
    {
        runState = ThreadState::PendingCleanup;
        Scheduler::Global()->RemoveThread(this->id);
        Scheduler::Global()->Yield();
    }

    void Thread::Kill()
    {}

    void Thread::Sleep(size_t millis)
    {}

    ThreadGroup* ThreadGroup::Current()
    {
        Thread* currentThread = Thread::Current();
        if (currentThread != nullptr)
            return currentThread->GetParent();
        return nullptr;
    }

    const sl::Vector<Thread*>& ThreadGroup::Threads() const
    { return threads; }

    const Thread* ThreadGroup::ParentThread() const
    { return parent; }

    size_t ThreadGroup::Id() const
    { return id; }

    Memory::VirtualMemoryManager* ThreadGroup::VMM()
    { return &vmm; }

    sl::Opt<size_t> ThreadGroup::AttachResource(ThreadResourceType type, sl::NativePtr resource)
    {
        if (type == ThreadResourceType::Empty || resource.ptr == nullptr)
            return {}; //haha very funny
        
        sl::ScopedSpinlock scopeLock(&lock);
        const size_t id = resourceIdAlloc.Alloc();
        while (resources.Size() <= id)
            resources.PushBack({ThreadResourceType::Empty, nullptr});
        resources[id] = ThreadResource(type, resource);

        return id;
    }

    bool ThreadGroup::DetachResource(size_t rid, bool force)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        if (rid > resources.Size())
            return false;
        if (resources[rid].type == ThreadResourceType::Empty)
            return false;

        //TODO: we need a way of tracking resource usage - someway to see if we can safely detach it.
        //TODO: we probably tell a resource it's been detatched. time for some polymorphism?
        resources[rid] = { ThreadResourceType::Empty, nullptr };
        resourceIdAlloc.Free(rid);
        return true;
    }

    sl::Opt<ThreadResource*> ThreadGroup::GetResource(size_t rid)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        if (rid > resources.Size() || resources[rid].type == ThreadResourceType::Empty)
            return {};
        
        return &resources[rid];
    }
}
