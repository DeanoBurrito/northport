#include <arch/Init.h>
#include <arch/Hat.h>
#include <arch/Timers.h>
#include <core/Config.h>
#include <core/Clock.h>
#include <core/Log.h>
#include <core/Pmm.h>
#include <core/Smp.h>
#include <core/WiredHeap.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <services/AcpiTables.h>
#include <services/MagicKeys.h>
#include <Exit.h>
#include <Maths.h>

namespace Npk
{
    uintptr_t hhdmBase;
    size_t hhdmLength;

    sl::SpinLock earlyVmLock;
    uintptr_t earlyVmBase;
    bool earlyVmEnabled;

    void* EarlyVmAlloc(uintptr_t paddr, size_t length, bool writable, bool mmio, const char* tag)
    {
        VALIDATE_(earlyVmEnabled, nullptr);

        sl::ScopedLock scopeLock(earlyVmLock);
        Log("EarlyVmAlloc: vaddr=0x%tx, paddr=0x%tx, len=0x%zx, tag=%s%s%s", LogLevel::Verbose, 
            earlyVmBase, paddr, length, tag, writable ? ", writable" : "", mmio ? ", mmio" : "");

        HatFlags flags = HatFlag::Global;
        if (writable)
            flags.Set(HatFlag::Write);
        if (mmio)
            flags.Set(HatFlag::Mmio);

        const size_t granuleSize = HatGetLimits().modes[0].granularity;
        const uintptr_t pBase = sl::AlignDown(paddr, granuleSize);
        const size_t pTop = paddr + length;
        for (size_t i = pBase; i < pTop; i += granuleSize)
            HatDoMap(KernelMap(), i - pBase + earlyVmBase, i, 0, flags, false);

        const uintptr_t retAddr = earlyVmBase;
        earlyVmBase = sl::AlignUp(earlyVmBase + (pTop - pBase), granuleSize);
        return reinterpret_cast<void*>(retAddr + (paddr - pBase));
    }

    uintptr_t EarlyVmControl(bool enable)
    {
        sl::ScopedLock scopeLock(earlyVmLock);
        earlyVmEnabled = enable;
        if (enable)
        {
            earlyVmBase = hhdmBase + hhdmLength;
            earlyVmBase += AlignUpPage(hhdmLength / PageSize() * sizeof(Core::PageInfo));
        }

        Log("EarlyVmControl: enabled=%s, base=0x%tx", LogLevel::Verbose, 
            enable ? "yes" : "no", earlyVmBase);
        return earlyVmBase;
    }

    void InitThread(void*)
    { ASSERT_UNREACHABLE(); }

    void ReclaimLoaderMemoryThread(void*)
    { ASSERT_UNREACHABLE(); }

    void PerCoreEntry(size_t myId, bool isBsp)
    {
        HatMakeActive(KernelMap(), true);
        ArchInitCore(myId);

        Core::InitLocalSmpMailbox();
        InitLocalTimers();
        //Core::InitLocalHeapCache();
        //Core::Pmm::Global().InitLocalCache();
        //TODO: intr routing, scheduler, local logging
        Core::InitLocalClockQueue(isBsp);
    }

    [[noreturn]]
    void ExitCoreInit()
    { 
        Log("core done", LogLevel::Debug);
        EnableInterrupts();
        Halt(); 
    }

    static void HandleMagicKeyPanic(npk_key_id key)
    {
        (void)key;
        Log("Manually triggered by magic key.", LogLevel::Fatal);
    }

    static void HandleMagicKeyShutdown(npk_key_id key)
    {
        (void)key;
        KernelExit(true);
    }

    extern "C" void KernelEntry()
    {
        earlyVmEnabled = false;
        ArchKernelEntry();
        Core::InitGlobalLogging();

        Log("Northport kernel started: v%zu.%zu.%zu for %s, compiled by %s from commit %s",
            LogLevel::Info, versionMajor, versionMinor, versionRev, targetArchStr,
            toolchainUsed, gitCommitShortHash);

        const size_t globalCtorCount = ((uintptr_t)INIT_ARRAY_END - (uintptr_t)INIT_ARRAY_BEGIN) / sizeof(void*);
        for (size_t i = 0; i < globalCtorCount; i++)
            INIT_ARRAY_BEGIN[i]();
        Log("Ran %zu global constructors.", LogLevel::Verbose, globalCtorCount);

        Core::InitConfigStore();
        ValidateLoaderData();

        ASSERT(GetHhdmBounds(hhdmBase, hhdmLength), "HHDM not provided by loader?");
        Log("Hhdm: base=0x%tx, length=0x%zx", LogLevel::Info, hhdmBase, hhdmLength);

        HatInit();
        HatMakeActive(KernelMap(), true);
        Core::Pmm::Global().Init();
        Core::InitWiredHeap();
        EarlyVmControl(true);

        if (auto rsdp = GetRsdp(); rsdp.HasValue())
            Services::SetRsdp(*rsdp);
        //if (auto fdt = GetDtb(); fdt.HasValue())
            //Services::SetFdtPoitner(*fdt); TODO: import smoldtb and do dtb stuff

        ArchLateKernelEntry();
        //Core::LateInitConfigStore();

        const bool enableAllMagics = Core::GetConfigNumber("kernel.enable_all_magic_keys", false);
        if (enableAllMagics || Core::GetConfigNumber("kernel.enable_panic_magic_key", false))
            Services::AddMagicKey(npk_key_id_p, HandleMagicKeyPanic);
        if (enableAllMagics || Core::GetConfigNumber("kerenl.enable_shutdown_magic_key", false))
            Services::AddMagicKey(npk_key_id_s, HandleMagicKeyShutdown);

        //driver subsystem, threading and vfs init

        StartupAps();
        ExitCoreInit();
    }
}
