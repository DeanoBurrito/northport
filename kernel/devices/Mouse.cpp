#include <devices/Mouse.h>
#include <Memory.h>
#include <Platform.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel::Devices
{
    constexpr size_t MouseBufferSize = 0x1000;
    constexpr size_t MouseEventCompactorHits = 10;
    
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

    void Mouse::FlushCompactor()
    {
        moveEvents->PushBack(eventCompactor);
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
        if (initialized)
            return;

        initialized = true;
        moveEvents = new sl::CircularQueue<sl::Vector2i>(MouseBufferSize);
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

    size_t Mouse::EventsPending()
    {
        return moveEvents->Size();
    }

    sl::Vector<sl::Vector2i> Mouse::GetEvents()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        FlushCompactor();

        const size_t bufferSize = moveEvents->Size();
        sl::Vector2i* buffer = new sl::Vector2i[bufferSize];
        const size_t poppedSize = moveEvents->PopInto(buffer, bufferSize);
        if (bufferSize != poppedSize)
            Log("Ruh roh, Mouse::GetEvents() got 2 different sizes despite holding the lock.", LogSeverity::Warning);

        //use owning ctor so buffer is freed properly
        return sl::Vector<sl::Vector2i>(buffer, poppedSize);
    }
}
