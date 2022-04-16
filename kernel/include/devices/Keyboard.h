#pragma once

#include <stdint.h>
#include <Optional.h>
#include <containers/CircularQueue.h>
#include <containers/Vector.h>
#include <devices/Keys.h>

namespace Kernel::Devices
{
    sl::Optional<int> GetPrintableChar(KeyEvent keyEvent);

    class Keyboard
    {
    private:
        char lock;
        sl::CircularQueue<KeyEvent>* keyEvents;

    public:
        static Keyboard* Global();
        void Init();

        void PushKeyEvent(const KeyEvent& event);
        size_t KeyEventsPending();
        sl::Vector<KeyEvent> GetKeyEvents();
    };
}
