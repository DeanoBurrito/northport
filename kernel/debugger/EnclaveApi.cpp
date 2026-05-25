#include <private/Debugger.hpp>
#include <Hardware.hpp>
#include <Core.hpp>
#include <lib/Printf.hpp>

namespace Npk::Private
{
    constexpr size_t MaxBreakpointClearFails = 64;

    static BreakpointList freeBreakpoints;
    static BreakpointList liveBreakpoints;
    static sl::Span<uintptr_t> perCpuStores;
    static sl::Span<char> debugLogRing;
    static size_t debugLogHead;

    sl::Atomic<sl::Flags<DebugEventType>> debugEventsMask;
    DebugProtocol* debugProtocol;
    bool debugHostWantsDisconnect = false;

    void InitInternalDebuggerApi(InitEventArg* arg)
    {
        for (size_t i = 0; i < arg->breakpoints.Size(); i++)
            freeBreakpoints.PushBack(&arg->breakpoints[i]);

        debugLogRing = arg->logring;
        perCpuStores = arg->perCpu;
    }

    void DebuggerPerCpuInit(void* arg)
    {
        auto* result = static_cast<sl::Atomic<NpkStatus>*>(arg);

        if (!HwInitDebugState())
            result->Store(NpkStatus::Unsupported, sl::Relaxed);
    }

    static void LogRingPutc(int c, void*)
    {
        if (debugLogRing.Empty())
            return;
        debugLogRing[debugLogHead % debugLogRing.Size()] = static_cast<char>(c);
        debugLogHead++;
    }

    void DebuggerLog(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        sl::VpPrintf(LogRingPutc, nullptr, format, args);
        va_end(args);
    }

    [[noreturn]]
    void DebuggerPanic(const char* format, ...)
    {
        sl::PPrintf(LogRingPutc, nullptr, "DEBUGGER PANIC: ");

        va_list args;
        va_start(args, format);
        sl::VpPrintf(LogRingPutc, nullptr, format, args);
        va_end(args);

        while (true)
            asm volatile("");
    }

    Breakpoint* AllocBreakpoint()
    {
        if (freeBreakpoints.Empty())
            return nullptr;

        auto bp = freeBreakpoints.PopFront();
        sl::MemSet(bp, 0, sizeof(*bp));
        new(bp) Breakpoint {};

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

            //read and write breakpoints have a length field encoded in `kind`.
            if ((it->read || it->write)
                && addr >= it->addr && addr < it->addr + it->kind)
                return &*it;
        }

        return nullptr;
    }

    bool ArmBreakpoint(Breakpoint& bp)
    {
        liveBreakpoints.PushBack(&bp);

        const auto result = HwEnableBreakpoint(bp.arch, bp.addr, bp.kind, 
            bp.read, bp.write, bp.execute, bp.hardware);

        if (!result)
            liveBreakpoints.Remove(&bp);

        return result;
    }

    bool DisarmBreakpoint(Breakpoint& bp)
    {
        const auto result = HwDisableBreakpoint(bp.arch, bp.addr, bp.kind);

        if (result)
            liveBreakpoints.Remove(&bp);

        return result;
    }

    void ClearAllBreakpoints()
    {
        size_t failCount = 0;
        while (!liveBreakpoints.Empty())
        {
            auto bp = liveBreakpoints.PopFront();
            if (HwDisableBreakpoint(bp->arch, bp->addr, bp->kind))
            {
                FreeBreakpoint(&bp);
                continue;
            }

            liveBreakpoints.PushBack(bp);
            failCount++;

            if (failCount >= MaxBreakpointClearFails)
                DebuggerPanic("ClearAllBreakpoints(): too many failures!");
        }
    }

    void NotifyOfHostDisconnect()
    {
        debugHostWantsDisconnect = true;
    }

    size_t GetDebugCpuCount()
    {
        return perCpuStores.Size() / PerCpuStorePointers;
    }

    sl::Span<uintptr_t> GetCpuDebugStores()
    {
        return perCpuStores;
    }

    TrapFrame* DebugFrameForCpu(CpuId id)
    {
        auto* status = RemoteStatus(id);
        if (status == nullptr)
            return nullptr;

        return status->lastIntrFrame;
    }

}
