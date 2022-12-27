#include <drivers/DriverManifest.h>
#include <drivers/DriverManager.h>
#include <drivers/builtin/BochsVga.h>

#define NAME_PCI_CLASS(cl, scl, pif) { 'p', 'c', 'i', 'c', c & 0xFF, scl & 0xFF, pif & 0xFF };
#define NAME_PCI_DEVICE(vendor, dev) { 'p', 'c', 'i', 'd', ((vendor) >> 8), (vendor) & 0xFF, ((dev) >> 8), (dev) & 0xFF }
#define NAME_DTB_COMPAT(compat) { "dtbc" compat }

namespace Npk::Drivers
{
    constexpr uint8_t bochsVgaName[] = NAME_PCI_DEVICE(0x1234, 0x1111);
    constexpr uint8_t virtioMmioFilter[] = NAME_DTB_COMPAT("virtio,mmio");
    constexpr uint8_t virtioGpuPciName[] = NAME_PCI_DEVICE(0x1AF4, 0x1050);
    constexpr uint8_t virtioGpuMmioName[] = { "VirtioMmio16" };

    void LoadBuiltInDrivers()
    {
        {   //bochs vga
            ManifestName name { sizeof(bochsVgaName), bochsVgaName };
            DriverManifest manifest { true, false, name, "bochs_vga", BochsVgaMain };
            DriverManager::Global().RegisterDriver(manifest);
        }

    }
}
