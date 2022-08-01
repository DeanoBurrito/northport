#include <devices/Keyboard.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>
#include <Panic.h>

namespace Kernel::Devices
{ 
    constexpr size_t KeyEventMaxCap = 512;
    constexpr size_t KeyEventDefaultCap = 256;

    void Keyboard::Deinit()
    {}

    void Keyboard::Reset()
    {}

    sl::Opt<Drivers::GenericDriver*> Keyboard::GetDriverInstance()
    { return {}; }

    Keyboard* globalKeyboardInstance = nullptr;
    Keyboard* Keyboard::Global()
    { 
        if (globalKeyboardInstance == nullptr)
            globalKeyboardInstance = new Keyboard();
        return globalKeyboardInstance;
    }

    void Keyboard::Init()
    {
        state = DeviceState::Ready;
        type = DeviceType::Keyboard;
        if (initialized)
            return;
        
        initialized = true;
        events.EnsureCapacity(KeyEventDefaultCap);
        eventSubscribers = new sl::Vector<size_t>();
        sl::SpinlockRelease(&lock);
    }

    void Keyboard::EventPump()
    {
        using namespace Scheduling;
        sl::Vector<ThreadGroupEvent> convEvents;
        {
            InterruptLock intLock;
            sl::ScopedSpinlock scopeLock(&lock);
            convEvents.EnsureCapacity(events.Size());

            for (size_t i = 0; i < events.Size(); i++)
            {
                convEvents.EmplaceBack(ThreadGroupEventType::KeyEvent, sizeof(KeyEvent), events[i].Pack());

                //to trigger a crash on ctrl+alt+shift+c
                if (events[i].id == KeyIdentity::C && events[i].tags == KeyTags::Pressed
                    && sl::EnumHasFlag(events[i].mods, KeyModFlags::BothControlsMask)
                    && sl::EnumHasFlag(events[i].mods, KeyModFlags::BothAltsMask)
                    && sl::EnumHasFlag(events[i].mods, KeyModFlags::BothShiftsMask))
                        Panic("Called via key combination ctrl+shift+alt+c.");
            }
            
            events.Clear();
        }
        
        for (size_t i = 0; i < eventSubscribers->Size(); i++)
        {
            ThreadGroup* destGroup = *Scheduler::Global()->GetThreadGroup(eventSubscribers->At(i));
            destGroup->PushEvents(convEvents);
        }
    }

    void Keyboard::PushKeyEvent(const KeyEvent& event)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        events.PushBack(event);

        if (events.Size() >= KeyEventMaxCap)
            events.Erase(0, KeyEventMaxCap / 4);
    }
}
