#include <devices/Mouse.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>

namespace Kernel::Devices
{
    constexpr size_t MouseBufferSize = 0x1000;
    constexpr size_t MouseEventCompactorHits = 10;
    constexpr size_t MouseEventMaxCap = 512;
    constexpr size_t MouseEventDefaultCap = 256;
    
    void Mouse::Deinit()
    {}

    void Mouse::Reset()
    {}

    sl::Opt<Drivers::GenericDriver*> Mouse::GetDriverInstance()
    { return {}; }

    size_t Mouse::ButtonCount()
    { return 0; }

    size_t Mouse::AxisCount()
    { return 0; }

    void Mouse::EventPump()
    {
        FlushCompactor();

        using namespace Scheduling;
        sl::Vector<ThreadGroupEvent> convEvents;
        {
            sl::ScopedSpinlock scopeLock(&lock);
            convEvents.EnsureCapacity(moveEvents.Size());

            for (size_t i = 0; i < moveEvents.Size(); i++)
                convEvents.EmplaceBack(ThreadGroupEventType::MouseEvent, sizeof(sl::Vector2i), nullptr);
            
            moveEvents.Clear();
        }

        for (size_t i = 0; i < eventSubscribers->Size(); i++)
        {
            ThreadGroup* destGroup = Scheduler::Global()->GetThreadGroup(eventSubscribers->At(i));
            destGroup->PushEvents(convEvents);
        }
    }

    void Mouse::FlushCompactor()
    {
        if (eventCompactor.x != 0 || eventCompactor.y != 0)
            moveEvents.PushBack(eventCompactor);
        eventCompactor.x = eventCompactor.y = 0;
        compactorHits = 0;
    }

    Mouse* globalMouseInstance = nullptr;
    Mouse* Mouse::Global()
    {
        if (globalMouseInstance == nullptr)
            globalMouseInstance = new Mouse();
        return globalMouseInstance;
    }

    void Mouse::Init()
    {
        state = DeviceState::Ready;
        if (initialized)
            return;

        initialized = true;
        eventSubscribers = new sl::Vector<size_t>();
        sl::SpinlockRelease(&lock);
    }

    void Mouse::PushMouseEvent(const sl::Vector2i relativeMove)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        
        //since mouse events can be sent thousands of times per second (on modern mice), or from multiple mice,
        //we compact multiple events into a single larger one. This looses some precision, but it's a tradeoff.
        //MouseEventCompactorSize determines how many events are rolled into one,
        //the compactor is also empied whenever GetEvents() is called.

        eventCompactor.x += relativeMove.x;
        eventCompactor.y += relativeMove.y;
        compactorHits++;

        if (compactorHits >= MouseEventCompactorHits)
            FlushCompactor();
    }
}
