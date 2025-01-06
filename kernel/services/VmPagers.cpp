#include <services/VmPagers.h>
#include <services/Vmm.h>
#include <core/WiredHeap.h>
#include <core/Log.h>
#include <String.h>

namespace Npk::Services
{
    struct MmioVmo
    {
        VmObject vmo;
        uintptr_t physBase;
        HatFlags hatFlags;
    };

    VmObject* CreateMmioVmo(uintptr_t paddr, size_t length, HatFlags hatFlags)
    {
        MmioVmo* vmo = NewWired<MmioVmo>();
        if (vmo == nullptr)
            return nullptr;

        vmo->physBase = paddr;
        vmo->hatFlags = hatFlags;
        vmo->vmo.length = length;
        vmo->vmo.isMmio = true;

        //TODO: do we want to keep a list of all mmio vmos? potential to reduce some overhead
        Log("Created VMO (mmio): paddr=0x%tx, length=0x%zx, hwFlags=0x%tx", LogLevel::Verbose,
            paddr, length, hatFlags.Raw());
        return &vmo->vmo;
    }

    bool DestroyMmioVmo(VmObject* vmo, bool force)
    {
        VALIDATE_(vmo != nullptr, true);
        VALIDATE_(vmo->refCount.count == 0, false);

        //TODO: unmap all views
        ASSERT_(vmo->views.Empty());

        auto mmio = reinterpret_cast<MmioVmo*>(vmo);
        Log("Destroyed VMO (mmio): paddr=0x%tx, length=0x%zx, hwFlags=0x%tx", LogLevel::Verbose,
            mmio->physBase, vmo->length, mmio->hatFlags.Raw());
        DeleteWired(mmio);
        return true;
    }

    sl::Opt<uintptr_t> GetMmioVmoPage(VmObject* vmo, size_t offset)
    {
        VALIDATE_(vmo != nullptr, {});
        VALIDATE_(vmo->isMmio, {});
        VALIDATE_(offset < vmo->length, {});

        auto mmio = reinterpret_cast<MmioVmo*>(vmo);
        return mmio->physBase + offset;
    }

    HatFlags GetMmioVmoHatFlags(VmObject* vmo, size_t offset)
    {
        VALIDATE_(vmo != nullptr, {});
        VALIDATE_(vmo->isMmio, {});
        VALIDATE_(offset <= vmo->length, {});

        auto mmio = reinterpret_cast<MmioVmo*>(vmo);
        return mmio->hatFlags;
    }
}
