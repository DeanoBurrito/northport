#pragma once

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
        sl::Vector<KeyEvent> events;

        void Deinit() override;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;
        void EventPump() override;

    public:
        static Keyboard* Global();
        void Init() override;

        void PushKeyEvent(const KeyEvent& event);
    };
}
