#pragma once

#include <drivers/DriverManifest.h>
#include <devices/PciBridge.h>

namespace Kernel::Devices::Pci
{
    namespace QemuStandardVga
    {
        void* InitNew(Drivers::DriverInitInfo* initInfo);
        void Destroy(void* inst);
        void HandleEvent(void* inst, Drivers::DriverEventType type, void* arg);
    }

    class QemuVgaDriver
    {
    private:
        PciFunction* pciDevice;

    public:
        QemuVgaDriver(Drivers::DriverInitInfo* info);
        ~QemuVgaDriver();

        QemuVgaDriver(const QemuVgaDriver& other) = delete;
        QemuVgaDriver& operator=(const QemuVgaDriver& other) = delete;
        QemuVgaDriver(QemuVgaDriver&& from) = delete;
        QemuVgaDriver& operator=(QemuVgaDriver&& from) = delete;

        void HandleEvent(Drivers::DriverEventType type, void* arg);
    };
}
