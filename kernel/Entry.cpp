#include <arch/Init.h>
#include <arch/Hat.h>
#include <core/Log.h>
#include <core/Config.h>
#include <core/Pmm.h>
#include <interfaces/intra/BakedConstants.h>
#include <interfaces/intra/LinkerSymbols.h>
#include <interfaces/loader/Generic.h>
#include <Handle.h>

namespace Npk
{
    uintptr_t hhdmBase;
    size_t hhdmLength;

    void InitThread(void*)
    {}

    void ReclaimLoaderMemoryThread(void*)
    {}

    void PerCoreEntry(size_t myId)
    {
        //local pmm and heap caches
    }

    [[noreturn]]
    void ExitCoreInit()
    { Halt(); }

    extern "C" void KernelEntry()
    {
        ArchKernelEntry();
        Core::InitGlobalLogging();

        Log("Northport kernel started: v%.zu.%zu.%zu for %s, compiled by %s from commit %s",
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
        //heap init
        //device discovery mechanisms: acpi tables and fdt (smoldtb) parsers

        //ArchLateKernelEntry();
        //Add early magic keys: P for panic
        //Core::LateInitConfigStore();
        //init early gterms

        //driver subsystem, threading and vfs init
        //startup APs, global timers, system clock

        Log("done for now", LogLevel::Debug);
        while (true)
        { asm(""); }
    }
}
