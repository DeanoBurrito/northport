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
#include <cpp/Asan.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <services/AcpiTables.h>
#include <services/MagicKeys.h>
#include <services/SymbolStore.h>
#include <services/Vmm.h>
#include <services/VmPagers.h>
#include <Exit.h>
#include <KernelThread.h>

namespace Npk
{
    //TODO: remove these hacks!
    uintptr_t reservedSwapMemoryBase;
    size_t reservedSwapMemoryLength;
    
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
            .vmm = {},
            .key = {},
            .base = reinterpret_cast<uintptr_t>(KERNEL_TEXT_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_TEXT_END) - reinterpret_cast<uintptr_t>(KERNEL_TEXT_BEGIN),
            .offset = 0,
            .lock = {},
            .flags = VmViewFlag::Exec,
        },
        {
            .vmmHook = {},
            .vmoHook = {},
            .vmoRef = {},
            .overlay = {},
            .vmm = {},
            .key = {},
            .base = reinterpret_cast<uintptr_t>(KERNEL_RODATA_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_RODATA_END) - reinterpret_cast<uintptr_t>(KERNEL_RODATA_BEGIN),
            .offset = 0,
            .lock = {},
            .flags = {},
        },
        {
            .vmmHook = {},
            .vmoHook = {},
            .vmoRef = {},
            .overlay = {},
            .vmm = {},
            .key = {},
            .base = reinterpret_cast<uintptr_t>(KERNEL_DATA_BEGIN),
            .length = reinterpret_cast<size_t>(KERNEL_DATA_END) - reinterpret_cast<uintptr_t>(KERNEL_DATA_BEGIN),
            .offset = 0,
            .lock = {},
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

        if (paddr == (uintptr_t)-1)
        {
            for (size_t i = 0; i < length; i += PageSize())
            {
                auto paddr = Core::PmAlloc();
                if (!paddr.HasValue())
                    return nullptr;
                if (HatDoMap(KernelMap(), earlyVmBase + i, *paddr, 0, flags) != HatError::Success)
                    return nullptr;
            }

            const uintptr_t newBase = sl::AlignUp(earlyVmBase + length, PageSize());
            void* retAddr = reinterpret_cast<void*>(earlyVmBase);
            earlyVmBase = newBase;
            return retAddr;
        }

        const uintptr_t pBase = sl::AlignDown(paddr, PageSize());
        const size_t pTop = paddr + length;
        for (size_t i = pBase; i < pTop; i += PageSize())
        {
            if (HatDoMap(KernelMap(), i - pBase + earlyVmBase, i, 0, flags) != HatError::Success)
                return nullptr;
        }

        const uintptr_t retAddr = earlyVmBase;
        earlyVmBase = sl::AlignUp(earlyVmBase + (pTop - pBase), PageSize());
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
        if (CoreLocalId() == 0)
            Services::LoadKernelSymbols();

        while (true)
        {
            Log("init is doing work, then sleeping for 1000ms", LogLevel::Debug);

            auto found = Services::FindSymbol(reinterpret_cast<uintptr_t>(&EarlyVmControl));
            if (found.HasValue())
                Log("init work: found %.*s", LogLevel::Debug, (int)found->info->name.Size(), found->info->name.Begin());

            sl::TimeCount timeout = 1000_ms;
            Core::WaitManager::WaitMany({}, nullptr, timeout, false);
        }

        Halt();
    }

    static void IdleThreadEntry(void* arg)
    {
        (void)arg;

        while (true)
        {
            Wfi();
            Core::SchedYield();
        }
    }

    void ReclaimLoaderMemoryThread(void*)
    { ASSERT_UNREACHABLE(); }

    void PerCoreEntry(size_t myId)
    {
        HatMakeActive(KernelMap(), true);
        HatInit(false);

        ArchInitCore(myId);
        Core::InitLocalSmpMailbox();
        //Core::Pmm::Global().InitLocalCache();

        InitLocalTimers();
        Core::InitLocalClockQueue();

        Core::Scheduler* localSched = NewWired<Core::Scheduler>();
        auto maybeIdle = CreateKernelThread(IdleThreadEntry, nullptr);
        ASSERT_(localSched != nullptr && maybeIdle.HasValue());

        localSched->Init(*maybeIdle);
        SetLocalPtr(SubsysPtr::Scheduler, localSched);

        auto maybeInitThread = CreateKernelThread(InitThreadEntry, nullptr);
        Core::SchedEnqueue(*maybeInitThread, 0);
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
#if NPK_HAS_KASAN
        InitAsan();
#endif

        Log("Northport kernel started: v%zu.%zu.%zu for %s, compiled by %s from commit %s%s",
            LogLevel::Info, versionMajor, versionMinor, versionRev, targetArchStr, 
            toolchainUsed, gitCommitShortHash, gitCommitDirty ? "-dirty" : "");

        const size_t globalCtorCount = ((uintptr_t)&INIT_ARRAY_END - (uintptr_t)&INIT_ARRAY_BEGIN) / sizeof(void*);
        for (size_t i = 0; i < globalCtorCount; i++)
            INIT_ARRAY_BEGIN[i]();
        Log("Ran %zu global constructors.", LogLevel::Verbose, globalCtorCount);

        Core::InitConfigStore();
        ValidateLoaderData();

        auto maybeSwap = EarlyPmAlloc(16 * MiB);
        if (maybeSwap.HasValue())
        {
            reservedSwapMemoryBase = *maybeSwap;
            reservedSwapMemoryLength = 16 * MiB;
            Log("Bad swap will use 0x%tx->0x%tx", LogLevel::Debug, 
                reservedSwapMemoryBase, reservedSwapMemoryBase + reservedSwapMemoryLength);
        }
        else
            reservedSwapMemoryLength = 0;

        ASSERT(GetHhdmBounds(hhdmBase, hhdmLength), "HHDM not provided by loader?");
        Core::InitGlobalLogging();
        Log("Hhdm: base=0x%tx, length=0x%zx", LogLevel::Info, hhdmBase, hhdmLength);

        HatInit(true);
        HatMakeActive(KernelMap(), true);
        Core::Pmm::Global().Init();
        Core::InitWiredHeap();
        EarlyVmControl(true);

        if (auto rsdp = GetRsdp(); rsdp.HasValue())
            Services::SetRsdp(*rsdp);
        //if (auto fdt = GetDtb(); fdt.HasValue())
            //Services::SetFdtPoitner(*fdt); TODO: import smoldtb and do dtb stuff

        Services::Vmm::InitKernel(kernelImage); //TODO: move this earlier, make sure we update usage of EarlyVmAlloc
        Services::InitSwap();
        //Core::LateInitConfigStore();
        ArchLateKernelEntry();

        const bool enableAllMagics = Core::GetConfigNumber("kernel.enable_all_magic_keys", false);
        if (enableAllMagics || Core::GetConfigNumber("kernel.enable_panic_magic_key", false))
            Services::AddMagicKey(npk_key_id_p, HandleMagicKeyPanic);
        if (enableAllMagics || Core::GetConfigNumber("kernel.enable_shutdown_magic_key", false))
            Services::AddMagicKey(npk_key_id_s, HandleMagicKeyShutdown);

        StartupAps();
        auto vmDaemonThread = CreateKernelThread(
                Services::Vmm().DaemonThreadEntry, &Services::KernelVmm().GetDomain());
        if (vmDaemonThread.HasValue())
            Core::SchedEnqueue(*vmDaemonThread, Core::SchedPriorityDefault());

        //TODO: vfs init, driver subsystem

        ExitCoreInit();
    }
}
