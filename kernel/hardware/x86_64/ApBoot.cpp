#include <private/Entry.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <hardware/x86_64/Private.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <lib/AcpiTypes.hpp>

namespace Npk
{
    static sl::Span<uint64_t> savedMtrrs;

    static bool IsValidLapicEntry(uint32_t* outId, const sl::MadtSource& source)
    {
        using namespace sl;

        if (source.type == MadtSourceType::LocalApic)
        {
            auto& entry = static_cast<const MadtSources::LocalApic&>(source);

            if (!entry.flags.Has(MadtSources::LocalApicFlag::Enabled))
                return false;
            
            if (outId != nullptr)
                *outId = entry.apicId;

            return true;
        }
        else if (source.type == sl::MadtSourceType::LocalX2Apic)
        {
            auto& entry = static_cast<const MadtSources::LocalX2Apic&>(source);

            if (!entry.flags.Has(MadtSources::LocalApicFlag::Enabled))
                return false;

            if (outId != nullptr)
                *outId = entry.apicId;

            return true;
        }

        return false;
    }

    size_t HwGetCpuCount()
    {
        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        if (!maybeMadt.HasValue())
            return 1;

        auto madt = static_cast<const sl::Madt*>(*maybeMadt);
        size_t accum = 0;

        for (auto source = sl::NextMadtSubtable(madt); source != nullptr;
            source = sl::NextMadtSubtable(madt, source))
        {
            if (IsValidLapicEntry(nullptr, *source))
                accum++;
        }

        NPK_ASSERT(accum > 0);

        return accum;
    }

    uint64_t HwGetMyWakeId()
    {
    }

    size_t HwEnumerateAps(sl::Span<HwWakeTarget> outAps)
    {
        auto maybeMadt = GetAcpiTable(sl::SigMadt);
        if (!maybeMadt.HasValue())
            return 0;

        auto madt = static_cast<const sl::Madt*>(*maybeMadt);
        size_t accum = 0;

        for (auto source = sl::NextMadtSubtable(madt); source != nullptr;
            source = sl::NextMadtSubtable(madt, source))
        {
            uint32_t lapicId = MyLapicId();
            if (!IsValidLapicEntry(&lapicId, *source))
                continue;
            if (lapicId == MyLapicId())
                continue;

            if (accum == outAps.Size())
                break;

            outAps[accum++].lapicId = lapicId;
        }

        return accum;
    }

    NpkStatus HwInitWaking(WakeTable& table, uintptr_t& virtBase)
    {
        if (CpuHasFeature(CpuFeature::Mtrr))
        {
            const uint64_t mtrrCap = ReadMsr(Msr::MtrrCap);
            const bool fixed = mtrrCap & 0x100;
            const size_t vcount = mtrrCap & 0xFF;
            
            Log("Saving BSP MTRR values: fixed=%s, vcnt=%zu", LogLevel::Info,
                fixed ? "yes (11 MTRRs)" : "no", vcount);

            const size_t mtrrCount = vcount * 2 + (fixed ? 11 : 0);
            NPK_ASSERT(mtrrCount * sizeof(uint64_t) < PageSize());

            auto storage = AllocPage(true);
            NPK_ASSERT(storage != nullptr);

            SetKernelMap(virtBase, LookupPagePaddr(storage), VmFlag::Write);
            savedMtrrs = sl::Span<uint64_t>(reinterpret_cast<uint64_t*>(
                virtBase), mtrrCount);
            virtBase += PageSize();

            SaveMtrrs(savedMtrrs);
        }

        //sanity checks
        NPK_ASSERT(kernelRoot >> 32 == 0);
        NPK_ASSERT(apBootPage < 1 * MiB);
        NPK_ASSERT(MyLapicVersion() >= 0x10);

        //patch the spinup blob, its part of the writable data section in the
        //kernel so we can access it directly.
        const size_t dataOffset = reinterpret_cast<uintptr_t>(ApSpinupBlobData)
            - reinterpret_cast<uintptr_t>(ApSpinupBlob);
        auto* spinupCr3 = reinterpret_cast<uint64_t*>(apBootPage + dataOffset);
        auto* spinupTablePtr = spinupCr3 + 1;
        auto* tableAddr = &table;

        sl::MemCopy(spinupCr3, &kernelRoot, sizeof(*spinupCr3));
        sl::MemCopy(spinupTablePtr, &tableAddr, sizeof(*spinupTablePtr));
        HwFlushCache(apBootPage + dataOffset, 16, HwCacheOp::Clean, 
            HwCacheType::DCache);

        return NpkStatus::Success;
    }

    NpkStatus HwWakeAp(HwWakeTarget target)
    {
        auto initDelay = 10_ms; //modern=0, old=10ms
        auto sipiDelay = 10_ms; //modern=10us, old=300us

        Log("Attempting to wake cpu with lapic id=%" PRIu32, LogLevel::Verbose,
            target.lapicId);

        SendIpi(target.lapicId, IpiType::Init, 0);
        StallFor(initDelay);
        if (!LastIpiSent())
            return NpkStatus::NotAvailable;

        SendIpi(target.lapicId, IpiType::InitDeAssert, 0);
        if (!LastIpiSent())
            return NpkStatus::NotAvailable;

        for (size_t i = 0; i < 2; i++)
        {
            SendIpi(target.lapicId, IpiType::Startup, apBootPage >> PfnShift());
            StallFor(sipiDelay);

            //NOTE: different error since we cant reliably say if the target cpu
            //will wake up or not.
            if (!LastIpiSent())
                return NpkStatus::InternalError;
        }

        return NpkStatus::Success;
    }

    void HwReportUnwokenAps(sl::Span<HwWakeTarget> targets, WakeTable& table)
    {
        //check if any targets that didnt claim a wake entry
        for (size_t i = 0; i < targets.Size(); i++)
        {
            bool claimed = false;
            for (size_t j = 0; j < table.entryCount && !claimed; j++)
            {
                const auto& entry = table.entries[j];

                if (entry.stage.Load(sl::Acquire) < WakeStage::Entered)
                    continue;
                if (entry.hwId == targets[i].lapicId)
                    claimed = true;
            }
            if (claimed)
                continue;

            Log("LAPIC-%" PRIu32" never claimed a wake entry, meaning it failed"
                " to boot.", LogLevel::Error, targets[i].lapicId);
        }

        //from the other direction: check if there are any wake entries with IDs
        //we dont recognise. This shouldn't happen but its easy to check so why
        //not, might avoid an awkward debugging session in the future.
        for (size_t i = 1; i < table.entryCount; i++)
        {
            const WakeEntry& entry = table.entries[i];

            if (entry.stage.Load(sl::Acquire) < WakeStage::Entered)
                continue;

            bool enumerated = false;
            for (size_t j = 0; j < targets.Size() && !enumerated; j++)
                enumerated = entry.hwId == targets[j].lapicId;

            if (enumerated)
                continue;

            Log("Cpu %zu woke as lapic-%" PRIu64 ", which was never a wake"
                " target.", LogLevel::Error, i, entry.hwId);
        }
    }

    void HwInitLocalEarly()
    {
        if (CpuHasFeature(CpuFeature::Mtrr) && MyCoreId() != 0)
            RestoreMtrrs(savedMtrrs);
        CommonCpuSetup();
    }

    void HwInitLocalLate()
    {
        LocalPvClockInit();
        InitTsc();
        NPK_ASSERT(InitApLapic());
        InitLocalAlarm();
    }
}
