#pragma once

#include <stdint.h>
#include <containers/CircularQueue.h>
#include <containers/Vector.h>
#include <Keys.h>
#include <devices/interfaces/GenericKeyboard.h>

namespace Kernel::Devices
{
    sl::Optional<int> GetPrintableChar(KeyEvent keyEvent);

    class Keyboard : public Interfaces::GenericKeyboard
    {
    private:
        char lock;
        bool initialized;
        sl::CircularQueue<KeyEvent>* keyEvents;

        void Deinit() override;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

    public:
        static Keyboard* Global();
        void Init() override;

        void PushKeyEvent(const KeyEvent& event);
        size_t EventsPending();
        sl::Vector<KeyEvent> GetEvents();
    };
}
