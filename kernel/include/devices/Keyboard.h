#pragma once

#include <stdint.h>

namespace Kernel::Devices
{
    //structed like 0xAA_BB_CCCC: AA is reserved (always zero), BB is key row, CCCC is key column.
    //I apologize to anyone who uses a more exciting keyboard layout, this is only the default. I welcome patches to add additional keys, and keyboard layouts.
    enum class KeyIdentity : uint32_t
    {
        Unknown = 0,

        //row 1 + extra f keys
        Escape          = 0x00'01'0001,
        F1              = 0x00'01'0002,
        F2              = 0x00'01'0003,
        F3              = 0x00'01'0004,
        F4              = 0x00'01'0005,
        F5              = 0x00'01'0006,
        F6              = 0x00'01'0007,
        F7              = 0x00'01'0008,
        F8              = 0x00'01'0009,
        F9              = 0x00'01'0010,
        F10             = 0x00'01'0011,
        F11             = 0x00'01'0012,
        F12             = 0x00'01'0013,
        F13             = 0x00'01'0014,
        F14             = 0x00'01'0015,
        F15             = 0x00'01'0016,
        F16             = 0x00'01'0017,
        F17             = 0x00'01'0018,
        F18             = 0x00'01'0019,
        F19             = 0x00'01'0020,
        F20             = 0x00'01'0021,
        PrintScreen     = 0x00'01'0022,
        ScrollLock      = 0x00'01'0023,
        SysPause        = 0x00'01'0024,

        //row 2
        Tilde           = 0x00'02'0001,
        Number1         = 0x00'02'0002,
        Number2         = 0x00'02'0003,
        Number3         = 0x00'02'0004,
        Number4         = 0x00'02'0005,
        Number5         = 0x00'02'0006,
        Number6         = 0x00'02'0007,
        Number7         = 0x00'02'0008,
        Number8         = 0x00'02'0009,
        Number9         = 0x00'02'0010,
        Number0         = 0x00'02'0011,
        Minus           = 0x00'02'0012,
        Equals          = 0x00'02'0013,
        Backspace       = 0x00'02'0014,
        Insert          = 0x00'02'0015,
        Home            = 0x00'02'0016,
        PageUp          = 0x00'02'0017,

        //row 3
        Tab                     = 0x00'03'0001,
        Q                       = 0x00'03'0002,
        W                       = 0x00'03'0003,
        E                       = 0x00'03'0004,
        R                       = 0x00'03'0005,
        T                       = 0x00'03'0006,
        Y                       = 0x00'03'0007,
        U                       = 0x00'03'0008,
        I                       = 0x00'03'0009,
        O                       = 0x00'03'0010,
        P                       = 0x00'03'0011,
        LeftSquareBracket       = 0x00'03'0012,
        RightSquareBracket      = 0x00'03'0013,
        BackSlash               = 0x00'03'0014,
        Delete                  = 0x00'03'0015,
        End                     = 0x00'03'0016,
        PageDown                = 0x00'03'0017,

        //row 4
        CapsLock        = 0x00'04'0001,
        A               = 0x00'04'0002,
        S               = 0x00'04'0003,
        D               = 0x00'04'0004,
        F               = 0x00'04'0005,
        G               = 0x00'04'0006,
        H               = 0x00'04'0007,
        J               = 0x00'04'0008,
        K               = 0x00'04'0009,
        L               = 0x00'04'0010,
        Semicolon       = 0x00'04'0011,
        SingleQuote     = 0x00'04'0012,
        Enter           = 0x00'04'0013,

        //row 5
        LeftShift       = 0x00'05'0001,
        Z               = 0x00'05'0002,
        X               = 0x00'05'0003,
        C               = 0x00'05'0004,
        V               = 0x00'05'0005,
        B               = 0x00'05'0006,
        N               = 0x00'05'0007,
        M               = 0x00'05'0008,
        Comma           = 0x00'05'0009,
        FullStop        = 0x00'05'0010,
        ForwardSlash    = 0x00'05'0011,
        RightShift      = 0x00'05'0012,
        ArrowUp         = 0x00'05'0013,

        //row 6
        LeftControl     = 0x00'06'0001,
        LeftGui         = 0x00'06'0002,
        LeftAlt         = 0x00'06'0003,
        Space           = 0x00'06'0004,
        RightAlt        = 0x00'06'0005,
        RightGui        = 0x00'06'0006,
        RightControl    = 0x00'06'0007,
        ArrowLeft       = 0x00'06'0008,
        ArrowDown       = 0x00'06'0009,
        ArrowRight      = 0x00'06'0010,

        //row 7 - numpad
        NumLock         = 0x00'07'0001,
        NumpadDivide    = 0x00'07'0002,
        NumpadMultiply  = 0x00'07'0003,
        NumpadMinus     = 0x00'07'0004,
        NumpadAdd       = 0x00'07'0005,
        NumpadEnter     = 0x00'07'0006,
        NumpadDot       = 0x00'07'0007,
        Numpad0         = 0x00'07'0008,
        Numpad1         = 0x00'07'0009,
        Numpad2         = 0x00'07'0010,
        Numpad3         = 0x00'07'0011,
        Numpad4         = 0x00'07'0012,
        Numpad5         = 0x00'07'0013,
        Numpad6         = 0x00'07'0014,
        Numpad7         = 0x00'07'0015,
        Numpad8         = 0x00'07'0016,
        Numpad9         = 0x00'07'0017,

        //row 8 - mouse buttons
        MouseLeft       = 0x00'08'0001,
        MouseMiddle     = 0x00'08'0002,
        MouseRight      = 0x00'08'0003,
        Mouse3          = 0x00'08'0004,
        Mouse4          = 0x00'08'0005,

        //row 9 - multimedia keys
        MediaSearch     = 0x00'09'0001,
        MediaFavourites = 0x00'09'0002,
        MediaRefresh    = 0x00'09'0003,
        MediaCalculator = 0x00'09'0004,
        MediaApps       = 0x00'09'0005,
        MediaHome       = 0x00'09'0006,
        PrevTrack       = 0x00'09'0007,
        NextTrack       = 0x00'09'0008,
        PlayPauseTrack  = 0x00'09'0009,
        StopTrack       = 0x00'09'0010,
        VolumeMute      = 0x00'09'0011,
        VolumeUp        = 0x00'09'0012,
        VolumeDown      = 0x00'09'0013,
    };

    enum class KeyModFlags : uint16_t
    {
        None = 0,

        LeftShift = (1 << 0),
        RightShift = (1 << 1),
        BothShiftsMask = (3 << 0),

        LeftAlt = (1 << 2),
        RightAlt = (1 << 3),
        BothAltsMask = (3 << 2),

        LeftControl = (1 << 4),
        RightControl = (1 << 5),
        BothControlsMask = (3 << 4),

        LeftGui = (1 << 6),
        RightGui = (1 << 7),
        BothGuisMask = (3 << 6),
    };

    enum class KeyTags : uint8_t
    {
        //key event for a modifier, can be ignored if you care about modifiers in relation to other keys (we track those)
        IsModifier = (1 << 0),
    };

    struct KeyEvent
    {
        KeyIdentity id;
        KeyModFlags mods;
        KeyTags tags;
        uint8_t inputDeviceId;

        KeyEvent() = default;
        KeyEvent(KeyIdentity identity, KeyModFlags mod, KeyTags tag) : id(identity), mods(mod), tags(tag), inputDeviceId(0)
        {}
    };
}
