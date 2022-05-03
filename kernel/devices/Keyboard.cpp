#include <devices/Keyboard.h>
#include <scheduling/Scheduler.h>
#include <Locks.h>

namespace Kernel::Devices
{ 
    constexpr size_t KeyEventMaxCap = 512;
    constexpr size_t KeyEventDefaultCap = 256;

#define RETURN_IF_CASE(test, retLower, retUpper) case KeyIdentity::test: { return shifted ? retUpper : retLower; }
    sl::Optional<int> GetPrintableChar(KeyEvent key)
    {
        bool shifted = sl::EnumHasFlag(key.mods, KeyModFlags::BothShiftsMask);

        switch (key.id)
        {
        RETURN_IF_CASE(Tilde, '`', '~')
        RETURN_IF_CASE(Number1, '1', '!')
        RETURN_IF_CASE(Number2, '2', '@')
        RETURN_IF_CASE(Number3, '3', '#')
        RETURN_IF_CASE(Number4, '4', '$')
        RETURN_IF_CASE(Number5, '5', '%')
        RETURN_IF_CASE(Number6, '6', '^')
        RETURN_IF_CASE(Number7, '7', '&')
        RETURN_IF_CASE(Number8, '8', '*')
        RETURN_IF_CASE(Number9, '9', '(')
        RETURN_IF_CASE(Number0, '0', ')')
        RETURN_IF_CASE(Minus, '-', '_')
        RETURN_IF_CASE(Equals, '=', '+')

        RETURN_IF_CASE(Tab, '\t', '\t')
        RETURN_IF_CASE(Q, 'q', 'Q')
        RETURN_IF_CASE(W, 'w', 'W')
        RETURN_IF_CASE(E, 'e', 'E')
        RETURN_IF_CASE(R, 'r', 'R')
        RETURN_IF_CASE(T, 't', 'T')
        RETURN_IF_CASE(Y, 'y', 'Y')
        RETURN_IF_CASE(U, 'u', 'U')
        RETURN_IF_CASE(I, 'i', 'I')
        RETURN_IF_CASE(O, 'o', 'O')
        RETURN_IF_CASE(P, 'p', 'P')
        RETURN_IF_CASE(LeftSquareBracket, '[', '{')
        RETURN_IF_CASE(RightSquareBracket, ']', '}')
        RETURN_IF_CASE(BackSlash, '\\', '|')
        
        RETURN_IF_CASE(A, 'a', 'A')
        RETURN_IF_CASE(S, 's', 'S')
        RETURN_IF_CASE(D, 'd', 'D')
        RETURN_IF_CASE(F, 'f', 'F')
        RETURN_IF_CASE(G, 'g', 'G')
        RETURN_IF_CASE(H, 'h', 'H')
        RETURN_IF_CASE(J, 'j', 'J')
        RETURN_IF_CASE(K, 'k', 'K')
        RETURN_IF_CASE(L, 'l', 'L')
        RETURN_IF_CASE(Semicolon, ';', ':')
        RETURN_IF_CASE(SingleQuote, '\'', '"')
        
        RETURN_IF_CASE(Z, 'z', 'Z')
        RETURN_IF_CASE(X, 'x', 'X')
        RETURN_IF_CASE(C, 'c', 'C')
        RETURN_IF_CASE(V, 'v', 'V')
        RETURN_IF_CASE(B, 'b', 'B')
        RETURN_IF_CASE(N, 'n', 'N')
        RETURN_IF_CASE(M, 'm', 'M')
        RETURN_IF_CASE(Comma, ',', '<')
        RETURN_IF_CASE(FullStop, '.', '>')
        RETURN_IF_CASE(ForwardSlash, '/', '?')

        RETURN_IF_CASE(Space, ' ', ' ')
        
        default: 
            return sl::Optional<int>(sl::OptNoType);
        };
    }
#undef RETURN_IF_CASE

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
            sl::ScopedSpinlock scopeLock(&lock);
            convEvents.EnsureCapacity(events.Size());

            for (size_t i = 0; i < events.Size(); i++)
                convEvents.EmplaceBack(ThreadGroupEventType::KeyEvent, sizeof(KeyEvent), nullptr);
            
            events.Clear();
        }
        
        for (size_t i = 0; i < eventSubscribers->Size(); i++)
        {
            ThreadGroup* destGroup = Scheduler::Global()->GetThreadGroup(eventSubscribers->At(i));
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
