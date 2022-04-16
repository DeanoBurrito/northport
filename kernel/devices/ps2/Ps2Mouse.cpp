#include <devices/ps2/Ps2Mouse.h>
#include <devices/ps2/Ps2Driver.h>

namespace Kernel::Devices::Ps2
{
    void Ps2Mouse::Init()
    {}

    void Ps2Mouse::Deinit()
    {}

    void Ps2Mouse::Reset()
    {
        Deinit();
        Init();
    }

    extern Ps2Driver* ps2DriverInstance;
    sl::Opt<Drivers::GenericDriver*> Ps2Mouse::GetDriverInstance()
    {
        if (ps2DriverInstance == nullptr)
            return {};
        return ps2DriverInstance;
    }

    size_t Ps2Mouse::ButtonCount()
    { return 0; }

    size_t Ps2Mouse::AxisCount()
    { return 0; }
}
