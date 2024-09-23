#pragma once

#include <arch/__Select.h>
#include <stdint.h>
#include <stddef.h>
#include <Span.h>
#include <core/RunLevels.h>

namespace Npk
{
    //each entry here is a place for the relevant subsystem to store core-local data
    enum class LocalPtr : size_t
    {
        IntrControl,
        Scheduler,
        Thread,
        UserVmm,
        Logs,
        HeapCache,
        IntrRouting,

        Count
    };

    //top-level core-local structure, the bulk of the data is stored via indirect
    //pointers in the subsystemPtrs[] array. Not ideal, but it prevents including
    //a number of other headers here, and polluting unrelated files by including
    //this one.
    struct CoreLocalInfo
    {
        void* scratch;
        void* nextStack;
        size_t id;
        RunLevel runLevel;
        Core::DpcQueue dpcs;
        Core::ApcQueue apcs;
        void* subsystemPtrs[(size_t)LocalPtr::Count];

        constexpr void*& operator[](LocalPtr index)
        { return subsystemPtrs[(size_t)index]; }
    };

    size_t PageSize();
    void ArchBeginPanic();
    void ExplodeKernelAndReset();

    //this struct represents any per-thread state not included in the TrapFrame.
    //This is generally things the kernel doesnt use, and is only saved/restored 
    //between user thread changes. This is just floating point and vector regs,
    //and maybe a few misc arch specific regs.
    struct ExtendedRegs;

    void InitExtendedRegs(ExtendedRegs** regs);
    void SaveExtendedRegs(ExtendedRegs* regs);
    void LoadExtendedRegs(ExtendedRegs* regs);
    bool ExtendedRegsFence();

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset = 0);

    CoreLocalInfo& CoreLocal();
    bool CoreLocalAvailable();
}

#ifdef NPK_ARCH_INCLUDE_MISC
#include NPK_ARCH_INCLUDE_MISC
#endif
