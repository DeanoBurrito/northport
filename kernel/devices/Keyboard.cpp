#include <devices/Keyboard.h>
#include <Memory.h>
#include <Platform.h>
#include <Log.h>
#include <Locks.h>

#define KEYBOARD_BUFFER_SIZE 0xFF

namespace Kernel::Devices
{ 

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
        if (initialized)
            return;
        
        initialized = true;
        keyEvents = new sl::CircularQueue<KeyEvent>(KEYBOARD_BUFFER_SIZE);
        sl::SpinlockRelease(&lock);
    }

    void Keyboard::PushKeyEvent(const KeyEvent& event)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        keyEvents->PushBack(event);
    }

    size_t Keyboard::EventsPending()
    {
        return keyEvents->Size();
    }

    sl::Vector<KeyEvent> Keyboard::GetEvents()
    {
        sl::ScopedSpinlock scopeLock(&lock);

        const size_t bufferSize = keyEvents->Size();
        KeyEvent* buffer = new KeyEvent[bufferSize];
        const size_t poppedSize = keyEvents->PopInto(buffer, bufferSize);
        if (bufferSize != poppedSize)
            Log("Keyboard::GetEvents() got 2 different buffer sizes despite scope lock.", LogSeverity::Warning);

        return sl::Vector<KeyEvent>(buffer, poppedSize); //use the size of the data that was returned, not what we allocated.
    }
}
