#pragma once

#include <devices/interfaces/GenericKeyboard.h>
#include <Keys.h>

namespace Kernel::Devices::Ps2
{
    class Ps2Driver;

    enum class Ps2ScancodeSet : uint8_t
    {
        Set1,
        Set2,
        Set3,
    };
    
    class Ps2Keyboard : public Interfaces::GenericKeyboard
    {
    friend Ps2Driver;
    protected:
        bool useSecondaryPort;
        size_t inputLength;
        uint8_t* inputBuffer;

        Ps2ScancodeSet currentSet;
        KeyModFlags currentModifiers;

        void Init() override;
        void Deinit() override;

        uint8_t ReadByte();
        void TranslateAndStore();
        void ApplyKeyTags(KeyEvent& ev, bool released);
        void UpdateModifiers(const KeyIdentity& id, bool released);
    
    public:
        ~Ps2Keyboard() = default;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;

        void HandleIrq();
    };
}
