#include <scheduling/ThreadGroup.h>
#include <scheduling/Thread.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Scheduling
{
    //Addresses chosen because it's easy to identity in memory dumps/traces. No other real significance.
    constexpr size_t UserGlobalStackBase = 0x55'5555'0000;
    constexpr size_t UserStackPages = 4;
    constexpr size_t KernelGlobalStackBase = 0xFFFF'D555'0000'0000;
    constexpr size_t KernelStackPages = 4;
    
    sl::NativePtr ThreadGroup::AllocUserStack()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        size_t index = userStackBitmap.FindAndClaimFirst();
        while (index == (size_t)-1)
        {
            userStackBitmap.Resize(userStackBitmap.Size() * 2);
            index = userStackBitmap.FindAndClaimFirst();
        }
        const sl::NativePtr base = UserGlobalStackBase + index * (UserStackPages + 1) * PAGE_FRAME_SIZE; //+1 because of the gaurd page we leave unmapped
        
        vmm.AddRange({ base.raw, UserStackPages * PAGE_FRAME_SIZE, MFlags::UserAccessible | MFlags::AllowWrites | MFlags::SystemRegion }, true);
        return base.raw + UserStackPages * PAGE_FRAME_SIZE;
    }

    void ThreadGroup::FreeUserStack(sl::NativePtr base)
    {
        base.raw -= UserStackPages * PAGE_FRAME_SIZE;
        if (!vmm.RangeExists({ base.raw, UserStackPages * PAGE_FRAME_SIZE }))
        {
            Log("Tried to free user stack: stack VM range not found.", LogSeverity::Warning);
            return;
        }
        
        sl::ScopedSpinlock scopeLock(&lock);
        vmm.RemoveRange({ base.raw, UserStackPages * PAGE_FRAME_SIZE });
        
        const size_t index = (base.raw - UserGlobalStackBase) / (UserStackPages + 1);
        userStackBitmap.Clear(index);
    }

    sl::NativePtr ThreadGroup::AllocKernelStack()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        size_t index = kernelStackBitmap.FindAndClaimFirst();
        while (index == (size_t)-1)
        {
            kernelStackBitmap.Resize(kernelStackBitmap.Size() * 2);
            index = kernelStackBitmap.FindAndClaimFirst();
        }
        const sl::NativePtr base = KernelGlobalStackBase + index * (KernelStackPages + 1) * PAGE_FRAME_SIZE;

        vmm.AddRange({ base.raw, KernelStackPages * PAGE_FRAME_SIZE, MFlags::AllowWrites }, true);
        return base.raw + KernelStackPages * PAGE_FRAME_SIZE;
    }

    void ThreadGroup::FreeKernelStack(sl::NativePtr base)
    {
        base.raw -= KernelStackPages * PAGE_FRAME_SIZE;
        if (!vmm.RangeExists({ base.raw, KernelStackPages * PAGE_FRAME_SIZE }))
        {
            Log("Tried to free kernel stack: stack VM range not found.", LogSeverity::Warning);
            return;
        }

        sl::ScopedSpinlock scopeLock(&lock);
        vmm.RemoveRange({ base.raw, KernelStackPages * PAGE_FRAME_SIZE });
        const size_t index = (base.raw - KernelGlobalStackBase) / (KernelStackPages + 1);
        kernelStackBitmap.Clear(index);
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
