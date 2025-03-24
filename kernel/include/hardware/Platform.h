#pragma once

#include <Types.h>
#include <Span.h>
#include <Optional.h>
#include <hardware/Arch.h>

namespace Npk
{
    void PalEarlyEntry();
    void PalMappingEntry(EarlyMmuEnvironment& env, uintptr_t& vmAllocHead);
    void PalLateEntry();
    void PalInitCore(size_t id);

    bool PalGetRandom(sl::Span<uint8_t> data);

    struct PalCpu
    {
        size_t interruptId; //id used by core local interrupt controller
        size_t configId; //id used by acpi or fdt, may be different to interruptId
        bool isBsp;
    };

    size_t PalGetCpus(sl::Span<PalCpu> store, size_t offset);
    void PalBootCpu(PalCpu cpu);

    struct MsiConfig
    {
        uintptr_t addr;
        uintptr_t data;
    };

    sl::Opt<MsiConfig> ConstructMsi(size_t core, size_t vector);
    bool DeconstructMsi(MsiConfig cfg, size_t& core, size_t& vector);

    bool SendIpi(size_t dest, bool urgent);
    void EmergencyReset();

    using TimerNanos = uint64_t;

    struct TimerCapabilities
    {
        bool timestampForUptime;
    };

    void GetTimeCapabilities(TimerCapabilities& caps);
    void SetAlarm(TimerNanos nanos);
    TimerNanos AlarmMax();
    TimerNanos GetTimestamp();
}
