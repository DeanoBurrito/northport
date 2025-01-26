#include <services/VmPagers.h>
#include <services/Vmm.h>
#include <services/BadSwap.h>
#include <core/WiredHeap.h>
#include <core/Log.h>
#include <String.h>

namespace Npk
{
    //TODO: remove these hacks!
    extern uintptr_t reservedSwapMemoryBase;
    extern size_t reservedSwapMemoryLength;
}

namespace Npk::Services
{
    sl::RwLock swapBackendsLock;
    size_t backendsCount = 0;
    SwapBackend* backends[1 << SwapKeyBackendBits];

    void InitSwap()
    {
        if (reservedSwapMemoryLength == 0)
            return;

        swapBackendsLock.WriterLock();
        backends[backendsCount++] = InitBadSwap(reservedSwapMemoryBase, reservedSwapMemoryLength);
        swapBackendsLock.WriterUnlock();
    }

    sl::Opt<SwapKey> ReserveSwap(size_t length)
    {
        swapBackendsLock.ReaderLock();
        for (size_t i = 0; i < backendsCount; i++)
        {
            SwapKey key;
            if (!backends[i]->Reserve(length, &key))
                continue;

            swapBackendsLock.ReaderUnlock();
            key.backend = i;
            return key;
        }
        swapBackendsLock.ReaderUnlock();

        return {};
    }

    void UnreserveSwap(SwapKey key, size_t length)
    {
        swapBackendsLock.ReaderLock();
        if (key.backend >= backendsCount)
        {
            swapBackendsLock.ReaderUnlock();
            Log("Invalid swap backend id: %u", LogLevel::Error, key.backend);
            return;
        }

        backends[key.backend]->Unreserve(key, length);
        swapBackendsLock.ReaderUnlock();
    }

    bool SwapOut(SwapKey key, size_t offset, uintptr_t paddr)
    {
        swapBackendsLock.ReaderLock();
        if (key.backend == NoSwap || key.backend >= backendsCount)
        {
            swapBackendsLock.ReaderUnlock();
            return false;
        }

        const bool success = backends[key.backend]->Write(key, offset, paddr);
        swapBackendsLock.ReaderUnlock();

        return success;
    }

    bool SwapIn(SwapKey key, size_t offset, uintptr_t paddr)
    {
        swapBackendsLock.ReaderLock();
        if (key.backend == NoSwap || key.backend >= backendsCount)
        {
            swapBackendsLock.ReaderUnlock();
            return false;
        }

        const bool success = backends[key.backend]->Read(key, offset, paddr);
        swapBackendsLock.ReaderUnlock();
        return success;
    }

    struct MmioVmo
    {
        VmObject vmo;
        uintptr_t physBase;
        HatFlags hatFlags;
    };

    VmObject* CreateMmioVmo(uintptr_t paddr, size_t length, HatFlags hatFlags)
    {
        MmioVmo* vmo = NewWired<MmioVmo>();
        VALIDATE_(vmo != nullptr, nullptr);

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
