#pragma once

#include <stddef.h>
#include <Platform.h>

#define PS2_CMD_READ_CONFIG 0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_KB_PORT 0xAE
#define PS2_CMD_DISABLE_KB_PORT 0xAD
#define PS2_CMD_ENABLE_MOUSE_PORT 0xA8
#define PS2_CMD_DISABLE_MOUSE_PORT 0xA7

namespace Kernel::Devices
{
    class Ps2Controller;

    class Ps2Device
    {
    friend Ps2Controller;
    protected:
        bool useSecondaryPort;
        bool available;

        void WriteByte(uint8_t data);
        uint8_t ReadByte(bool ignoreOutBuffStatus = false);

    public:
        virtual void Init(bool usePort2) = 0;
        virtual void HandleIrq() = 0;
    };

    enum class Ps2ScancodeSet : uint8_t
    {
        Set1,
        Set2,
        Set3,
    };
    
    class Ps2Keyboard : public Ps2Device
    {
    private:
        constexpr static inline size_t inputMaxLenth = 4;
        size_t inputLength;
        uint8_t* inputBuffer;
        Ps2ScancodeSet currentSet;

        void Translate();

    public:
        void Init(bool usePort2) override;
        void HandleIrq() override;
    };

    class Ps2Mouse : public Ps2Device
    {
    public:
        void Init(bool usePort2) override;
        void HandleIrq() override;
    };
    
    //we assume standard ps/2 setup here (kb = first port, mouse = second).
    class Ps2Controller
    {
    friend Ps2Device;
    private:
        static bool OutputBufferEmpty();
        static bool InputBufferEmpty();

    public:
        //assumed to be first port
        static Ps2Keyboard* Keyboard();
        //assumed to be second port
        static Ps2Mouse* Mouse();

        //checks if controller is available, and initializes it. Returns number of ports available (0 if not avaible, 1 = kb only, 2 = kb + mouse), does not init them.
        static size_t InitController();
    };
}
