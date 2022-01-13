#include <devices/pci/QemuStandardVga.h>
#include <Log.h>

namespace Kernel::Devices::Pci
{
    //peak c++ efficiency right here ;-;
    namespace QemuStandardVga
    {
        void* InitNew(Drivers::DriverInitInfo* initInfo)
        { return new QemuVgaDriver(initInfo); }

        void Destroy(void* inst)
        { delete static_cast<QemuVgaDriver*>(inst); }

        void HandleEvent(void* inst, Drivers::DriverEventType type, void* arg)
        { static_cast<QemuVgaDriver*>(inst)->HandleEvent(type, arg); }
    }

    QemuVgaDriver::QemuVgaDriver(Drivers::DriverInitInfo* info)
    {
        Log("Hello from qemu std vga! :D", LogSeverity::Info);
    }

    QemuVgaDriver::~QemuVgaDriver()
    {}

    void QemuVgaDriver::HandleEvent(Drivers::DriverEventType type, void* arg)
    {}
}
