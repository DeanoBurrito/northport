#include <DebuggerPrivate.hpp>
#include <Hardware.hpp>
#include <Memory.hpp>

namespace Npk::Private
{
    static BreakpointList freeBreakpoints;
    static BreakpointList liveBreakpoints;

    void DebuggerPerCpuInit(void* arg)
    {
        auto* result = static_cast<sl::Atomic<DebugStatus>*>(arg);

        if (!HwInitDebugState())
            result->Store(DebugStatus::NotSupported);
    }

    Breakpoint* AllocBreakpoint()
    {
        if (freeBreakpoints.Empty())
            return nullptr;

        auto bp = freeBreakpoints.PopFront();
        sl::MemSet(bp, 0, sizeof(*bp));

        return bp;
    }

    void FreeBreakpoint(Breakpoint** bp)
    {
        if (bp == nullptr || *bp == nullptr)
            return;

        freeBreakpoints.PushFront(*bp);
        *bp = nullptr;
    }

    Breakpoint* GetBreakpointByAddr(uintptr_t addr)
    {
        for (auto it = liveBreakpoints.Begin(); it != liveBreakpoints.End();
            ++it)
        {
            if (addr == it->addr)
                return &*it;

            //read or write breakpoints have a length field, we should do
            //a range-based check on those.
            if ((it->read || it->write) 
                && addr >= it->addr && addr < it->addr + it->kind)
                return &*it;
        }

        return nullptr;
    }

    bool ArmBreakpoint(Breakpoint& bp)
    {
        return HwEnableBreakpoint(bp.arch, bp.addr, bp.kind, bp.read, 
            bp.write, bp.execute, bp.hardware);
    }

    bool DisarmBreakpoint(Breakpoint& bp)
    {
        return HwDisableBreakpoint(bp.arch, bp.addr, bp.kind);
    }
}
