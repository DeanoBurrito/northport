#include <private/Entry.hpp>
#include <private/Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr uint32_t IdAllocClosed = 1ul << 30;
    constexpr size_t DefaultBootTimeoutMs = 500;

    static sl::Span<HwWakeTarget> wakeTargets;
    static WakeTable* wakeTable;
    static sl::Atomic<size_t> onlineCpus = 1;
    static sl::Atomic<bool> unleashAps;

    static void ApEntry(WakeEntry* entry)
    {
        const CpuId myId = entry - wakeTable->entries;
        HwSetMyLocals(entry->locals, myId);

        entry->hwId = HwGetMyWakeId();
        entry->stage.Store(WakeStage::Entered, sl::Release);

        Log("Core %zu is online.", LogLevel::Info, MyCoreId());

        HwInitLocalEarly();
        InitLocalState(entry->tempMapHwToken, entry->tempSlotsBase,
            entry->tempSlotsCount, entry->tempMapBase);
        HwInitLocalLate();

        ThreadContext idleContext {};
        Private::InitLocalScheduler(&idleContext);
        SetCurrentThread(&idleContext);

        IntrsOn();
        entry->stage.Store(WakeStage::Online, sl::Release);
        onlineCpus.Add(1, sl::Release);

        while (!unleashAps.Load(sl::Acquire))
            sl::HintSpinloop();

        entry->stage.Store(WakeStage::Released, sl::Release);
        EnterIdleLoop();
    }

    size_t StartAps(const PerCpuConfig& conf, uintptr_t& virtBase)
    {
        if (conf.cpuCount == 1)
        {
            Log("Skipping AP wakeup, only 1 cpu detected.", LogLevel::Info);

            return 0;
        }

        //none of these are necessary, they just catch refactors upstream that
        //may miss something (there is a story here, yes).
        NPK_ASSERT(conf.localsBase != 0 && conf.localsStride != 0);
        NPK_ASSERT(conf.apStacksBase != 0 && conf.stackStride != 0);
        NPK_ASSERT(conf.tempMapBase != 0 && conf.tempMapStride != 0);
        NPK_ASSERT(conf.tempSlotsBase != 0 && conf.tempSlotsStride != 0);
        NPK_ASSERT(conf.tempSlotsCount != 0);
        NPK_ASSERT(conf.tempMapTokens.Size() >= conf.cpuCount - 1);

        //1. Allocate wakeTable
        const size_t tableLen = sizeof(WakeTable) + conf.cpuCount
            * sizeof(WakeEntry);
        wakeTable = reinterpret_cast<WakeTable*>(virtBase);

        auto prevIpl = RaiseIpl(Ipl::Dpc);
        for (size_t i = 0; i < tableLen; i += PageSize())
        {
            auto* page = AllocPage(true);
            NPK_ASSERT(page != nullptr);
            auto paddr = LookupPagePaddr(page);

            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);
            virtBase += PageSize();
        }
        if (prevIpl < Ipl::Dpc)
            LowerIpl(prevIpl);

        //2. Populate wake table
        wakeTable->entryCount = conf.cpuCount;
        wakeTable->entryStride = sizeof(WakeEntry);
        wakeTable->nextCpuId.Store(1, sl::Relaxed);
        wakeTable->lateCpus.Store(0, sl::Relaxed);

        for (size_t i = 1; i < conf.cpuCount; i++)
        {
            const size_t index = i - 1;
            auto& entry = wakeTable->entries[i];

            entry.hwData = 0;
            entry.entry = reinterpret_cast<uintptr_t>(&ApEntry);
            entry.stackBase = conf.apStacksBase + conf.stackStride * index;
            entry.stackLen = KernelStackSize();
            entry.locals = conf.localsBase + conf.localsStride * index;
            entry.tempMapBase = conf.tempMapBase + conf.tempMapStride * index;
            entry.tempSlotsBase = conf.tempSlotsBase + conf.tempSlotsStride
                * index;
            entry.tempSlotsCount = conf.tempSlotsCount;
            entry.tempMapHwToken = conf.tempMapTokens[index];
        }
        wakeTable->entries[0].hwId = HwGetMyWakeId();
        wakeTable->entries[0].stage.Store(WakeStage::Online, sl::Release);

        Log("AP wake table at %p, %zu entries", LogLevel::Info, wakeTable,
            conf.cpuCount);

        //3. Allocate space for wake targets and have it populated.
        const size_t targetsLen = sizeof(HwWakeTarget) * (conf.cpuCount - 1);
        wakeTargets = { reinterpret_cast<HwWakeTarget*>(virtBase),
            conf.cpuCount - 1 };

        prevIpl = RaiseIpl(Ipl::Dpc);
        for (size_t i = 0; i < targetsLen; i += PageSize())
        {
            auto* page = AllocPage(true);
            NPK_ASSERT(page != nullptr);
            auto paddr = LookupPagePaddr(page);

            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);
            virtBase += PageSize();
        }
        if (prevIpl < Ipl::Dpc)
            LowerIpl(prevIpl);

        wakeTargets = wakeTargets.Subspan(0, HwEnumerateAps(wakeTargets));
        Log("%zu APs available for waking.", LogLevel::Verbose,
            wakeTargets.Size());

        auto result = HwInitWaking(*wakeTable, virtBase);
        NPK_ASSERT(result == NpkStatus::Success);

        //4. Send wake ups to all cores.
        size_t expected = wakeTargets.Size();
        for (size_t i = 0; i < wakeTargets.Size(); i++)
        {
            Log("Attempting to wake target %zu", LogLevel::Trace, i);

            result = HwWakeAp(wakeTargets[i]);
            if (result == NpkStatus::Success)
            {
                Log("Successfully sent wake request to target %zu",
                    LogLevel::Trace, i);

                continue;
            }

            NPK_UNEXPECTED_STATUS(result, LogLevel::Warning);
            
            //`NotAvailable` means the hardware layer couldn't deliver the wake
            //request or the AP isn't eligible for it. It's a signal that cpu
            //definitely wont come online later.
            if (result == NpkStatus::NotAvailable)
                expected--;
        }

        auto WaitForAps = [&](size_t targets, sl::TimePoint deadline) -> bool
            {
                while (onlineCpus.Load(sl::Acquire) < targets)
                {
                    if (deadline < GetMonotonicTime())
                        return false;
                    sl::HintSpinloop();
                }

                return true;
            };

        const size_t timeoutMs = ReadConfigUint("npk.cpu_wake_timeout",
            DefaultBootTimeoutMs);
        const auto deadline = GetMonotonicTime()
            + sl::TimeCount(sl::TimeScale::Millis, timeoutMs);

        WaitForAps(expected + 1, deadline);

        size_t claimed = wakeTable->nextCpuId.Exchange(IdAllocClosed,
            sl::AcqRel);
        if (claimed > wakeTable->entryCount)
        {
            Log("%zu APs claimed an id, only %" PRIu32" were expected",
                LogLevel::Error, claimed - 1, wakeTable->entryCount - 1);

            claimed = wakeTable->entryCount;
        }

        if (!WaitForAps(claimed, deadline))
        {
            Log("AP bringup deadline (%zums) before all cores reported alive",
                LogLevel::Error, timeoutMs);
        }

        //5. Report any late cores or failures to boot.
        const size_t lateCount = wakeTable->lateCpus.Load(sl::Acquire);
        if (lateCount != 0)
        {
            Log("%zu cpu%s booted late, wont be usable by the kernel",
                LogLevel::Error, lateCount, lateCount == 1 ? "" : "s");
        }

        if (claimed - 1 < expected)
            HwReportUnwokenAps(wakeTargets, *wakeTable);

        size_t holes = 0;
        for (size_t i = 1; i < claimed; i++)
        {
            const auto& entry = wakeTable->entries[i];
            const auto stage = entry.stage.Load(sl::Acquire);

            if (stage == WakeStage::Online)
                continue; //cpu is where it should be

            holes++;
            Log("Cpu %zu (hardware ID %" PRIu64") failed to wake, stage=%u",
                LogLevel::Error, i, entry.hwId, stage);
        }
        NPK_ASSERT(holes == 0);

        Log("AP startup done, %zu of %zu cpus online", LogLevel::Info,
            claimed, conf.cpuCount);

        return claimed - 1;
    }

    void ReleaseAps()
    {
        unleashAps.Store(true, sl::Release);

        if (wakeTable != nullptr)
            wakeTable->entries[0].stage.Store(WakeStage::Released, sl::Release);
    }
}
