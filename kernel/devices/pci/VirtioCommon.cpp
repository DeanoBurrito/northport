#include <devices/pci/VirtioCommon.h>
#include <devices/pci/PciCommon.h>
#include <devices/PciBridge.h>

namespace Kernel::Devices::Pci
{
    sl::Opt<VirtioPciCommonConfig*> GetVirtioPciCommonConfig(PciAddress addr)
    {
        const PciFunction* function = PciBridge::Global()->GetFunction(addr).Value();
        VirtioPciCommonConfig* config = nullptr;

        auto maybeCap = FindPciCap(addr, CapIdVendor);
        while (maybeCap)
        {
            VirtioPciCap* cap = static_cast<VirtioPciCap*>(*maybeCap);
            if (cap->configType == VirtioPciCapType::CommonConfig)
            {
                config = sl::NativePtr(function->Header()->bars[cap->bar].address).As<VirtioPciCommonConfig>(cap->offset);
                break;
            }

            maybeCap = FindPciCap(addr, CapIdVendor, *maybeCap);
        }

        if (config == nullptr)
            return {};

        return EnsureHigherHalfAddr(config);
    }
}
