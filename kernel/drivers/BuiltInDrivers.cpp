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
            ManifestName name { sizeof(bochsVgaName), bochsVgaName };
            DriverManifest manifest { true, false, name, "bochs_vga", BochsVgaMain };
            DriverManager::Global().RegisterDriver(manifest);
        }

        {
            //filter for detecting virtio-over-mmio devices
            ManifestName name { sizeof(virtioMmioFilter), virtioMmioFilter };
            DriverManifest manifest { true, true, name, "virtio_mmio", VirtioMmioFilterMain };
            DriverManager::Global().RegisterDriver(manifest);
        }

        {   //virtio gpu
            ManifestName namePci { sizeof(virtioGpuPciName), virtioGpuPciName };
            DriverManifest manifestPci { true, false, namePci, "virtio_gpu_pci", VirtioGpuMain };
            DriverManager::Global().RegisterDriver(manifestPci);

            ManifestName nameMmio { sizeof(virtioGpuMmioName), virtioGpuMmioName };
            DriverManifest manifestMmio { true, false, nameMmio, "virtio_gpu_mmio", VirtioGpuMain };
            DriverManager::Global().RegisterDriver(manifestMmio);
        }

        {   //NVMe
            ManifestName name { sizeof(nvmeName), nvmeName };
            DriverManifest manifest { true, false, name, "nvme", NvmeMain };
            DriverManager::Global().RegisterDriver(manifest);
        }
    }
}
