#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Locks.h>

namespace Npk
{
    enum class CpuFeature : size_t
    {
        VGuest,
#ifdef __x86_64__
        NoExecute,
        Pml3Translation,
        GlobalPages,
        Smap,
        Smep,
        Umip,
        Apic,
        ApicX2,
        FxSave,
        XSave,
        FPU,
        SSE,
        SSE2,
        AlwaysRunningApic,
        Tsc,
        TscDeadline,
        InvariantTsc,
#elif defined(__riscv)
        Sstc,
        SingleFPU,
        DoubleFPU,
        QuadFPU,
#endif

        Count
    };
    
    struct CpuDomain;
    struct NumaDomain;

    struct ThreadDomain
    {
        ThreadDomain* next;
        CpuDomain* parent;

        size_t id;
    };

    struct CpuDomain
    {
        CpuDomain* next;
        NumaDomain* parent;
        ThreadDomain* threads;

        size_t id;
        bool online;
    };

    struct MemoryDomain
    {
        MemoryDomain* next;
        NumaDomain* parent;

        uintptr_t base;
        size_t length;

        bool online;
    };

    struct NumaDomain
    {
        NumaDomain* next;
        CpuDomain* cpus;
        MemoryDomain* memory;

        sl::RwLock cpusLock;
        sl::RwLock memLock;
        size_t id;
    };

    void InitTopology(); //TODO: remove this hack

    //Detects system topology from the local core's perspective.
    void ScanLocalTopology();

    //Returns the root domain for this system.
    NumaDomain* GetTopologyRoot();

    //Detects, caches and logs the available feature-set of the local CPU.
    void ScanLocalCpuFeatures();

    //Determines if the current (local) CPU has a certain feature.
    bool CpuHasFeature(CpuFeature feature);

    //Vanity function, converts a feature tag into a string representation.
    const char* CpuFeatureName(CpuFeature feature);
}

