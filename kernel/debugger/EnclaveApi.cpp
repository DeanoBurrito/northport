#include <private/Debugger.hpp>
#include <Hardware.hpp>
#include <Core.hpp>
#include <lib/Printf.hpp>

namespace Npk::Private
{
    constexpr size_t MaxBreakpointClearFails = 64;
    constexpr size_t DebugVarMaxNameLength = 31;

    struct DebugVariable
    {
        sl::ListHook hook;
        char name[DebugVarMaxNameLength];
        uint8_t nameLength;
        uintptr_t value;
    };

    using DebugVariableList = sl::List<DebugVariable, &DebugVariable::hook>;

    static BreakpointList freeBreakpoints;
    static BreakpointList liveBreakpoints;
    static DebugVariableList freeVariables;
    static DebugVariableList liveVariables;
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

    NpkStatus CreateDebugVariable(DebugVariable** created, sl::StringSpan name,
        uintptr_t value)
    {
        if (name.Empty())
            return NpkStatus::InvalidArg;
        if (created == nullptr)
            return NpkStatus::InvalidArg;

        if (name[0] == DebugVarNamePrefix)
            name = name.Subspan(1, -1);
        if (name.Size() > DebugVarMaxNameLength)
            return NpkStatus::InvalidArg;

        if (freeVariables.Empty())
            return NpkStatus::Shortage;

        auto var = freeVariables.PopFront();
        sl::MemSet(var, 0, sizeof(*var));
        new (var) DebugVariable {};

        sl::MemCopy(var->name, name.Begin(), name.Size());
        var->nameLength = name.Size();
        var->value = value;

        liveVariables.PushBack(var);
        *created = var;

        return NpkStatus::Success;
    }

    NpkStatus DestroyDebugVariable(DebugVariable** var)
    {
        if (var == nullptr || *var == nullptr)
            return NpkStatus::InvalidArg;

        liveVariables.Remove(*var);
        freeVariables.PushFront(*var);

        *var = nullptr;

        return NpkStatus::Success;
    }

    NpkStatus LookupDebugVariable(DebugVariable** found, sl::StringSpan name)
    {
        if (found == nullptr)
            return NpkStatus::InvalidArg;
        if (name.Empty())
            return NpkStatus::InvalidArg;

        if (name[0] == DebugVarNamePrefix)
            name = name.Subspan(1, -1);
        if (name.Size() > DebugVarMaxNameLength)
            return NpkStatus::InvalidArg;

        for (auto it = liveVariables.Begin(); it != liveVariables.End(); ++it)
        {
            auto varName = sl::StringSpan(it->name, it->nameLength);

            if (varName != name)
                continue;
            *found = &*it;

            return NpkStatus::Success;
        }

        return NpkStatus::NotFound;
    }

    NpkStatus ReadDebugVariable(uintptr_t* value, DebugVariable& var)
    {
        if (value == nullptr)
            return NpkStatus::InvalidArg;

        *value = var.value;

        return NpkStatus::Success;
    }

    NpkStatus WriteDebugVariable(DebugVariable& var, uintptr_t value,
        uintptr_t* prevValue)
    {
        if (prevValue != nullptr)
            *prevValue = var.value;
        var.value = value;

        return NpkStatus::Success;
    }
}
