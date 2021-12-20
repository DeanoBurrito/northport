#include <devices/Ps2Controller.h>
#include <devices/Keyboard.h>
#include <devices/IoApic.h>
#include <Log.h>

#define BEGIN_BREAK_PACKET 0xF0
#define BEGIN_EXTENDED_PACKET 0xE0

namespace Kernel::Devices
{
    void Ps2Keyboard::Init(bool usePort2)
    {
        useSecondaryPort = usePort2;
        available = true;
        inputBuffer = new uint8_t[inputMaxLenth];
        inputLength = 0;
        currentSet = Ps2ScancodeSet::Set2;

        //map whatever used to be old school irq1 to our keyboard idt number
        auto overrideDetails = IoApic::TranslateToGsi(1);
        IoApic::Global(overrideDetails.gsiNum)->WriteRedirect(0, overrideDetails);
        IoApic::Global(overrideDetails.gsiNum)->SetPinMask(overrideDetails.irqNum, false);
    }

    void Ps2Keyboard::HandleIrq()
    {
        //we're in set2 here, so 1 byte make packets/2 byte break packets. +1 byte for extended keycodes
        //extended make begins with 0xE0, extended keyboards lead with 0xF0 (after extended byte).
        //there are some exceptions: pause and print screen, we just ignore those as they're a crazy 7 bytes long (why)

        uint8_t incoming = ReadByte(true); //can ignore output status flag, since an interrupt was triggered.
        if (incoming == BEGIN_BREAK_PACKET && inputLength > 0)
        {
            //Incoplete existing data, not sure why it wasnt translated, drop it.
            Log("Ps2 keyboard dropped partial packet data before BEGIN_BREAK_PACKET.", LogSeverity::Warning);
            inputLength = 0;
        }

        inputBuffer[inputLength] = incoming;
        inputLength++;

        if (inputBuffer[inputLength - 1] != BEGIN_BREAK_PACKET && inputBuffer[inputLength - 1] != BEGIN_EXTENDED_PACKET)
        {
            Translate(); //we know if those were last written, there's more to the packet, otherwise try translate it
        }

        if (inputLength == inputMaxLenth)
            inputLength = 0; //full packet, but it couldnt be translated. Drop it.
    }

    using KI = KeyIdentity; //trying to make this easy on myself
    constexpr static inline KeyIdentity ps2Set2Identities[] = 
    {
        KI::Unknown,        KI::F9,             KI::Unknown,        KI::F5,
        KI::F3,             KI::F1,             KI::F2,             KI::F12,
        KI::Unknown,        KI::F10,            KI::F8,             KI::F6,
        KI::F4,             KI::Tab,            KI::Tilde,          KI::Unknown,

        KI::Unknown,        KI::LeftAlt,        KI::LeftShift,      KI::Unknown,
        KI::LeftControl,    KI::Q,              KI::Number1,        KI::Unknown,
        KI::Unknown,        KI::Unknown,        KI::Z,              KI::S,
        KI::A,              KI::W,              KI::Number2,        KI::Unknown,
        
        KI::Unknown,        KI::C,              KI::X,              KI::D,
        KI::E,              KI::Number4,        KI::Number3,        KI::Unknown,
        KI::Unknown,        KI::Space,          KI::V,              KI::F,
        KI::T,              KI::R,              KI::Number5,        KI::Unknown,

        KI::Unknown,        KI::N,              KI::B,              KI::H,
        KI::G,              KI::Y,              KI::Number6,        KI::Unknown,
        KI::Unknown,        KI::Unknown,        KI::M,              KI::J,
        KI::U,              KI::Number7,        KI::Number8,        KI::Unknown,

        KI::Unknown,        KI::Comma,          KI::K,              KI::I,
        KI::O,              KI::Number0,        KI::Number9,        KI::Unknown,
        KI::Unknown,        KI::FullStop,       KI::ForwardSlash,   KI::L,
        KI::Semicolon,      KI::P,              KI::Minus,          KI::Unknown,

        KI::Unknown,        KI::Unknown,        KI::SingleQuote,    KI::Unknown,
        KI::LeftSquareBracket, KI::Equals,      KI::Unknown,        KI::Unknown, //I felt a great disturbance in the ki
        KI::CapsLock,       KI::RightShift,     KI::Enter,          KI::RightSquareBracket,
        KI::Unknown,        KI::BackSlash,      KI::Unknown,        KI::Unknown,

        KI::Unknown,        KI::Unknown,        KI::Unknown,        KI::Unknown, //amazing use of memory, thanks whoever designed these scancode sets
        KI::Unknown,        KI::Unknown,        KI::Backspace,      KI::Unknown, 
        KI::Unknown,        KI::Numpad1,        KI::Unknown,        KI::Numpad4,
        KI::Numpad7,        KI::Unknown,        KI::Unknown,        KI::Unknown,

        KI::Numpad0,        KI::NumpadDot,      KI::Numpad2,        KI::Numpad5,
        KI::Numpad6,        KI::Numpad8,        KI::Escape,         KI::NumLock,
        KI::F11,            KI::NumpadAdd,      KI::Numpad3,        KI::NumpadMinus,
        KI::NumpadMultiply, KI::Numpad9,        KI::ScrollLock,     KI::Unknown,

        KI::Unknown,        KI::Unknown,        KI::Unknown,        KI::F7,
    };

#define RETURN_IF_CASE(test, retval) case test: return KeyIdentity::retval;
    constexpr KeyIdentity GetExtendedKeyIdentity(uint8_t byte)
    {
        //more space and time efficient than a sparse array in this case
        switch (byte)
        {
        RETURN_IF_CASE(0x10, MediaSearch)
        RETURN_IF_CASE(0x1F, LeftGui)
        RETURN_IF_CASE(0x20, MediaRefresh)
        RETURN_IF_CASE(0x21, VolumeDown)
        RETURN_IF_CASE(0x23, VolumeMute)
        RETURN_IF_CASE(0x27, RightGui)
        RETURN_IF_CASE(0x2B, MediaCalculator)
        RETURN_IF_CASE(0x2F, MediaApps)
        RETURN_IF_CASE(0x32, VolumeUp)
        RETURN_IF_CASE(0x34, PlayPauseTrack)
        RETURN_IF_CASE(0x3A, MediaHome)
        RETURN_IF_CASE(0x4A, NumpadDivide)
        RETURN_IF_CASE(0x4D, NextTrack)
        RETURN_IF_CASE(0x5A, NumpadEnter)
        RETURN_IF_CASE(0x69, End)
        RETURN_IF_CASE(0x6B, ArrowLeft)
        RETURN_IF_CASE(0x6C, Home)
        RETURN_IF_CASE(0x70, Insert)
        RETURN_IF_CASE(0x71, Delete)
        RETURN_IF_CASE(0x72, ArrowDown)
        RETURN_IF_CASE(0x74, ArrowRight)
        RETURN_IF_CASE(0x75, ArrowUp)
        RETURN_IF_CASE(0x7A, PageDown)
        RETURN_IF_CASE(0x7D, PageUp)

        default:
            return KeyIdentity::Unknown;
        }
    }
#undef RETURN_IF_CASE

    void Ps2Keyboard::ApplyKeyTags(KeyEvent& ev, bool released)
    {
        ev.tags = KeyTags::None;
        
        switch (ev.id)
        {
        case KeyIdentity::LeftShift:
        case KeyIdentity::RightShift:
        case KeyIdentity::LeftAlt:
        case KeyIdentity::RightAlt:
        case KeyIdentity::LeftControl:
        case KeyIdentity::RightControl:
        case KeyIdentity::LeftGui:
        case KeyIdentity::RightGui:
            ev.tags = sl::EnumSetFlag(ev.tags, KeyTags::IsModifier);
            break;

        default:
            break;
        };

        if (released)
            ev.tags = sl::EnumSetFlag(ev.tags, KeyTags::Released);
        else
            ev.tags = sl::EnumSetFlag(ev.tags, KeyTags::Pressed);
    }

    void Ps2Keyboard::UpdateModifiers(const KeyIdentity& id, bool released)
    {
        switch (id)
        {
        case KeyIdentity::LeftShift:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::LeftShift, released);
            break;
        case KeyIdentity::RightShift:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::RightShift, released);
            break;
        case KeyIdentity::LeftAlt:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::LeftAlt, released);
            break;
        case KeyIdentity::RightAlt:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::RightAlt, released);
            break;
        case KeyIdentity::LeftControl:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::LeftControl, released);
            break;
        case KeyIdentity::RightControl:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::RightControl, released);
            break;
        case KeyIdentity::LeftGui:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::LeftGui, released);
            break;
        case KeyIdentity::RightGui:
            currentModifiers = sl::EnumSetFlagState(currentModifiers, KeyModFlags::RightGui, released);
            break;
        default:
            return;
        }
    }

    void Ps2Keyboard::Translate()
    {    
        //easy early exits
        if (inputBuffer[0] == BEGIN_BREAK_PACKET && inputLength < 2)
            return;
        if (inputBuffer[0] == BEGIN_EXTENDED_PACKET && inputLength < 2)
            return;
        if (inputLength == 2 && inputBuffer[0] == BEGIN_BREAK_PACKET && inputBuffer[1] == BEGIN_EXTENDED_PACKET)
            return;

        size_t scan = 0;
        bool released = false;
        if (inputBuffer[0] == BEGIN_BREAK_PACKET)
        {
            scan = 1;
            released = true;
        }

        KeyIdentity keyId;
        if (inputBuffer[scan] == BEGIN_EXTENDED_PACKET)
            keyId = GetExtendedKeyIdentity(inputBuffer[scan + 1]);
        else
            keyId = ps2Set2Identities[inputBuffer[scan]];

        UpdateModifiers(keyId, released);
        
        //apply input metadata
        KeyEvent e;
        e.id = keyId;
        e.mods = currentModifiers;
        e.inputDeviceId = (uint8_t)BuiltInInputDevices::Ps2Keyboard;
        ApplyKeyTags(e, released);

        //store it in global keyboard 
        Keyboard::Global()->PushKeyEvent(e);
        
        //reset input
        inputLength = 0;
    }
}
