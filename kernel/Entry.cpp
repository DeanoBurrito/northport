#include <arch/Init.h>
#include <arch/Hat.h>
#include <arch/Timers.h>
#include <arch/Interrupts.h>
#include <core/Config.h>
#include <core/Clock.h>
#include <core/Log.h>
#include <core/Pmm.h>
#include <core/Scheduler.h>
#include <core/Event.h>
#include <core/Smp.h>
#include <core/WiredHeap.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <services/AcpiTables.h>
#include <services/MagicKeys.h>
#include <services/Vmm.h>
#include <Exit.h>
#include <KernelThread.h>

namespace Npk
{
    uintptr_t hhdmBase;
    size_t hhdmLength;

    sl::SpinLock earlyVmLock;
    uintptr_t earlyVmBase;
    bool earlyVmEnabled;

    Services::VmView kernelImage[] =
    {
        {
            .vmmHook = {},
            .vmoHook = {},
            .vmoRef = {},
            .overlay = {},
            .offset = 0,
            .base = reinterpret_cast<uintptr_t>(KERNEL_TEXT_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_TEXT_END) - reinterpret_cast<uintptr_t>(KERNEL_TEXT_BEGIN),
            .flags = VmViewFlag::Exec,
        },
        {
            .vmmHook = {},
            .vmoHook = {},
            .vmoRef = {},
            .overlay = {},
            .offset = 0,
            .base = reinterpret_cast<uintptr_t>(KERNEL_RODATA_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_RODATA_END) - reinterpret_cast<uintptr_t>(KERNEL_RODATA_BEGIN),
            .flags = {},
        },
        {
            .vmmHook = {},
            .vmoHook = {},
            .vmoRef = {},
            .overlay = {},
            .offset = 0,
            .base = reinterpret_cast<uintptr_t>(KERNEL_DATA_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_DATA_END) - reinterpret_cast<uintptr_t>(KERNEL_DATA_BEGIN),
            .flags = VmViewFlag::Write,
        },
    };

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
        if (paddr == (uintptr_t)-1)
        {
            for (size_t i = 0; i < length; i += granuleSize)
            {
                auto paddr = Core::PmAlloc();
                if (!paddr.HasValue())
                    return nullptr;
                if (HatDoMap(KernelMap(), earlyVmBase + i, *paddr, 0, flags, false) != HatError::Success)
                    return nullptr;
            }

            const uintptr_t newBase = sl::AlignUp(earlyVmBase + length, granuleSize);
            void* retAddr = reinterpret_cast<void*>(earlyVmBase);
            earlyVmBase = newBase;
            return retAddr;
        }

        const uintptr_t pBase = sl::AlignDown(paddr, granuleSize);
        const size_t pTop = paddr + length;
        for (size_t i = pBase; i < pTop; i += granuleSize)
        {
            if (HatDoMap(KernelMap(), i - pBase + earlyVmBase, i, 0, flags, false) != HatError::Success)
                return nullptr;
        }

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

    static void InitThreadEntry(void* arg)
    { 
        (void)arg;
        Log("Init thread up", LogLevel::Debug);

        while (true)
        {
            WaitEntry entry;
            sl::ScaledTime timeout = 1000_ms;
            Core::WaitManager::WaitMany({}, &entry, timeout, false);
            Log("Tick!", LogLevel::Debug);
        }

        Halt();
    }

    static void IdleThreadEntry(void* arg)
    {
        (void)arg;
        Halt();
    }

    void ReclaimLoaderMemoryThread(void*)
    { ASSERT_UNREACHABLE(); }

    void PerCoreEntry(size_t myId, bool isBsp)
    {
        HatMakeActive(KernelMap(), true);
        ArchInitCore(myId);
        Core::InitLocalSmpMailbox();
        Core::Pmm::Global().InitLocalCache();

        InitLocalTimers();
        Core::InitLocalClockQueue(isBsp);

        Core::Scheduler* localSched = NewWired<Core::Scheduler>();
        auto maybeIdle = CreateKernelThread(IdleThreadEntry, nullptr);
        ASSERT_(localSched != nullptr && maybeIdle.HasValue());

        localSched->Init(*maybeIdle);
        SetLocalPtr(SubsysPtr::Scheduler, localSched);

        //TODO: init local intr routing, logging, heap caches, launch init thread + reclaim thread
    }

    [[noreturn]]
    void ExitCoreInit()
    { 
        Core::Scheduler::Local()->Kickstart();
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
        Core::InitGlobalLogging();
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

        Services::Vmm::InitKernel(kernelImage); //TODO: move this earlier, make sure we update usage of EarlyVmAlloc
        //Core::LateInitConfigStore();

        const bool enableAllMagics = Core::GetConfigNumber("kernel.enable_all_magic_keys", false);
        if (enableAllMagics || Core::GetConfigNumber("kernel.enable_panic_magic_key", false))
            Services::AddMagicKey(npk_key_id_p, HandleMagicKeyPanic);
        if (enableAllMagics || Core::GetConfigNumber("kernel.enable_shutdown_magic_key", false))
            Services::AddMagicKey(npk_key_id_s, HandleMagicKeyShutdown);

        //driver subsystem, threading and vfs init

        StartupAps();

        auto maybeInitThread = CreateKernelThread(InitThreadEntry, nullptr);
        Core::SchedEnqueue(*maybeInitThread, 0);
        ExitCoreInit();
    }
}
