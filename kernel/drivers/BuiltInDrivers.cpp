#include <drivers/DriverManifest.h>
#include <drivers/DriverManager.h>
#include <drivers/builtin/BochsVga.h>
#include <drivers/builtin/VirtioGpu.h>
#include <drivers/builtin/VirtioTransport.h>
#include <drivers/builtin/Nvme.h>

#define NAME_PCI_CLASS(cl, scl, pif) { 'p', 'c', 'i', 'c', (cl) & 0xFF, (scl) & 0xFF, (pif) & 0xFF };
#define NAME_PCI_DEVICE(vendor, dev) { 'p', 'c', 'i', 'd', ((vendor) >> 8), (vendor) & 0xFF, ((dev) >> 8), (dev) & 0xFF }
#define NAME_DTB_COMPAT(compat) { "dtbc" compat }

namespace Npk::Drivers
{
    constexpr uint8_t bochsVgaName[] = NAME_PCI_DEVICE(0x1234, 0x1111);
    constexpr uint8_t virtioMmioFilter[] = NAME_DTB_COMPAT("virtio,mmio");
    constexpr uint8_t virtioGpuPciName[] = NAME_PCI_DEVICE(0x1AF4, 0x1050);
    constexpr uint8_t virtioGpuMmioName[] = { "VirtioMmio16" };
    constexpr uint8_t nvmeName[] = NAME_PCI_CLASS(1, 8, 2);

    void LoadBuiltInDrivers()
    {
        {   //bochs vga
            DriverManifest manifest(bochsVgaName, "BochsVga", BochsVgaMain);
            DriverManager::Global().RegisterDriver(manifest);
        }

        {
            //filter for detecting virtio-over-mmio devices
            DriverManifest manifest(virtioMmioFilter, "VirtioMmioFilter", VirtioMmioFilterMain);
            DriverManager::Global().RegisterDriver(manifest);
        }

        {   //virtio gpu
            DriverManifest pciManifest(virtioGpuPciName, "VirtioGpu", VirtioGpuMain);
            DriverManifest mmioManifest(virtioGpuMmioName, "VirtioGpu", VirtioGpuMain);
            DriverManager::Global().RegisterDriver(pciManifest);
            DriverManager::Global().RegisterDriver(mmioManifest);
        }

        {   //NVMe
            DriverManifest manifest(nvmeName, "Nvme", NvmeMain);
            DriverManager::Global().RegisterDriver(manifest);
        }
    }
}
