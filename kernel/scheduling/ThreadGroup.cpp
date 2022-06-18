#include <scheduling/ThreadGroup.h>
#include <scheduling/Thread.h>
#include <memory/PhysicalMemory.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Scheduling
{
    //address chosen because it's easy to identity in memory dumps/traces. No other real significance.
    constexpr size_t userGlobalStackBase = 0x55'5555'0000;
    constexpr size_t userStackPages = 4;
    // constexpr size_t kernelGlobalStackBase = 0xFFFF'D555'0000'0000; //TODO: use virtual kernel stacks instead of hhdm ones
    constexpr size_t kernelStackPages = 4;
    
    sl::NativePtr ThreadGroup::AllocUserStack()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        size_t index = userStackBitmap.FindAndClaimFirst();
        while (index == (size_t)-1)
        {
            userStackBitmap.Resize(userStackBitmap.Size() + userStackBitmap.Size() * 2);
            index = userStackBitmap.FindAndClaimFirst();
        }
        const sl::NativePtr base = userGlobalStackBase + index * (userStackPages + 1) * PAGE_FRAME_SIZE; //+1 because of the gaurd page we leave unmapped
        
        using MFlags = Memory::MemoryMapFlags;
        vmm.AddRange({ base.raw, userStackPages * PAGE_FRAME_SIZE, MFlags::UserAccessible | MFlags::AllowWrites | MFlags::SystemRegion }, true);
        return base.raw + userStackPages * PAGE_FRAME_SIZE;
    }

    void ThreadGroup::FreeUserStack(sl::NativePtr base)
    {
        base.raw -= userStackPages * PAGE_FRAME_SIZE;
        if (!vmm.RangeExists({ base.raw, userStackPages * PAGE_FRAME_SIZE }))
        {
            Log("Tried to free user stack: stack VM range not found.", LogSeverity::Warning);
            return;
        }
        
        sl::ScopedSpinlock scopeLock(&lock);
        vmm.RemoveRange({ base.raw, userStackPages * PAGE_FRAME_SIZE });
        
        const size_t index = (base.raw - userGlobalStackBase) / (userStackPages + 1);
        userStackBitmap.Clear(index);
    }

    sl::NativePtr ThreadGroup::AllocKernelStack()
    {
        //TODO: would be nice to move away from allocating stacks in the hhdm.
        const sl::NativePtr base = EnsureHigherHalfAddr(Memory::PMM::Global()->AllocPages(kernelStackPages));
        return base.raw + kernelStackPages * PAGE_FRAME_SIZE;
    }

    void ThreadGroup::FreeKernelStack(sl::NativePtr base)
    {
        base.raw -= kernelStackPages * PAGE_FRAME_SIZE;
        Memory::PMM::Global()->FreePages(EnsureLowerHalfAddr(base.raw), kernelStackPages);
    }
    
    ThreadGroup* ThreadGroup::Current()
    {
        Thread* currentThread = Thread::Current();
        if (currentThread != nullptr)
            return currentThread->Parent();
        return nullptr;
    }

    sl::Opt<size_t> ThreadGroup::AttachResource(ThreadResourceType type, sl::NativePtr resource)
    {
        if (type == ThreadResourceType::Empty || resource.ptr == nullptr)
            return {}; //haha very funny
        
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);
        
        const size_t id = resourceIdAlloc.Alloc();
        while (resources.Size() <= id)
            resources.PushBack({ThreadResourceType::Empty, nullptr});
        resources[id] = ThreadResource(type, resource);

        return id;
    }

    bool ThreadGroup::DetachResource(size_t rid)
    {
        InterruptLock intLock;
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
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        if (rid > resources.Size() || resources[rid].type == ThreadResourceType::Empty)
            return {};
        
        return &resources[rid];
    }

    sl::Opt<size_t> ThreadGroup::FindResource(sl::NativePtr data)
    {
        for (size_t i = 0; i < resources.Size(); i++)
        {
            if (resources[i].type == ThreadResourceType::Empty)
                continue;

            if (resources[i].res.raw == data.raw)
                return i;
        }

        return {};
    }

    size_t ThreadGroup::PendingEventCount() const
    { return events.Size(); }

    void ThreadGroup::PushEvent(const ThreadGroupEvent& event)
    {
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        events.PushBack(event);

        for (size_t i = 0; i < threads.Size(); i++)
        {
            if (threads[i]->runState == ThreadState::SleepingForEvents)
            {
                threads[i]->runState = ThreadState::Running;
                threads[i]->wakeTime = 0;
            }
        }
    }

    void ThreadGroup::PushEvents(const sl::Vector<ThreadGroupEvent>& eventList)
    {
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0 ;i < eventList.Size(); i++)
            events.EmplaceBack(eventList[i]);

        for (size_t i = 0; i < threads.Size(); i++)
        {
            if (threads[i]->runState == ThreadState::SleepingForEvents)
            {
                threads[i]->runState = ThreadState::Running;
                threads[i]->wakeTime = 0;
            }
        }
    }

    sl::Opt<ThreadGroupEvent> ThreadGroup::PeekEvent()
    {
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        if (events.Empty())
            return {};
        return events.Front();
    }

    sl::Opt<ThreadGroupEvent> ThreadGroup::ConsumeEvent()
    {
        InterruptLock intLock;
        sl::ScopedSpinlock scopeLock(&lock);

        if (events.Empty())
            return {};
        
        ThreadGroupEvent ev = events.Front();
        events.Erase(0);
        return ev;
    }
}
