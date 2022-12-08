#include <drivers/DriverManifest.h>
#include <drivers/DriverManager.h>
#include <drivers/builtin/BochsVga.h>

#define NAME_PCIC(cl, scl, pif) { 'p', 'c', 'i', 'c', c & 0xFF, scl & 0xFF, pif & 0xFF };
#define NAME_PCID(vendor, dev) { 'p', 'c', 'i', 'd', ((vendor) >> 8), (vendor) & 0xFF, ((dev) >> 8), (dev) & 0xFF }

namespace Npk::Drivers
{
    constexpr uint8_t bochsVgaName[] = NAME_PCID(0x1234, 0x1111);

    void LoadBuiltInDrivers()
    {
        {   //bochs vga
            ManifestName name { sizeof(bochsVgaName), bochsVgaName };
            DriverManifest manifest { true, name, "Bochs VGA", BochsVgaMain };
            DriverManager::Global().RegisterDriver(manifest);
        }

        //ps2 peripherals
        //virtio input
        //virtio gpu
    }
}
