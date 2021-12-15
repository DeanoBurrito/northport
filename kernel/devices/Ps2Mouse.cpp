#include <devices/Ps2Controller.h>

namespace Kernel::Devices
{
    void Ps2Mouse::Init(bool usePort2)
    {
        useSecondaryPort = usePort2;
    }
    
    void Ps2Mouse::HandleIrq()
    {}
}
