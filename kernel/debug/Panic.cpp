#include <debug/Panic.h>
#include <debug/Log.h>
#include <debug/Symbols.h>
#include <drivers/DriverManager.h>
#include <interrupts/Ipi.h>
#include <tasking/RunLevels.h>
#include <tasking/Threads.h>
#include <NanoPrintf.h>

namespace Npk::Debug
{
    constexpr const char* ExceptFormatStr = "Unhandled exception: %s, stack=0x%lx, flags=0x%x, s=0x%lx\r\n";
    constexpr const char* CoreFormatStr = "Core %lu: runLevel %lu (%s), logs=%p\r\n";
    constexpr const char* ProgramFormatStr = "Thread %lu.%lu: name=%.*s, driverShadow=%.*s\r\n";
    constexpr const char* TraceFrameFormatStr = "%3u: 0x%016lx %.*s!%.*s+0x%lx\r\n";
    constexpr const char* ResetStr = "\r\nSystem has halted indefinitely, manual reset required.";
    constexpr size_t MaxTraceDepth = 16;
    constexpr int MaxProgramNameLen = 16;
    constexpr int MaxSymbolNameLen = 52;
    constexpr size_t AcquireOutputLockAttempts = 4321'0000;

    constexpr const char* ExceptionStrs[] = 
    {
        "memory access",
        "invalid instruction",
        "exit request",
        "bad operation",
        "breakpoint",
    };

    sl::Atomic<size_t> panicFlag;
    sl::Span<LogOutput*> panicOutputs;

    static void PanicPrint(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const size_t length = npf_vsnprintf(nullptr, 0, format, args) + 1;
        va_end(args);

        char buffer[length];
        va_start(args, format);
        npf_vsnprintf(buffer, length, format, args);
        va_end(args);

        for (size_t i = 0; i < panicOutputs.Size(); i++)
        {
            if (panicOutputs[i]->Write != nullptr)
                panicOutputs[i]->Write({ buffer, length });
        }
    }

    static void PrintPanicHeader()
    {
        PanicPrint("\r\n");
        PanicPrint("       )\r\n");
        PanicPrint("    ( /(                        (\r\n");
        PanicPrint("    )\\())  (   (             (  )\\             )        (\r\n");
        PanicPrint("   ((_)\\  ))\\  )(    (      ))\\((_)  `  )   ( /(   (    )\\   (\r\n");
        PanicPrint("  (_ ((_)/((_)(()\\   )\\ )  /((_)_    /(/(   )(_))  )\\ )((_)  )\\\r\n");
        PanicPrint("  | |/ /(_))   ((_) _(_/( (_)) | |  ((_)_\\ ((_)_  _(_/( (_) ((_)\r\n");
        PanicPrint("  | ' < / -_) | '_|| ' \\))/ -_)| |  | '_ \\)/ _` || ' \\))| |/ _|\r\n");
        PanicPrint("  |_|\\_\\\\___| |_|  |_||_| \\___||_|  | .__/ \\__,_||_||_| |_|\\__|\r\n");
        PanicPrint("                                    |_|\r\n");
    }

    static void BeginPanic()
    {
        const size_t continuePanic = panicFlag.FetchAdd(1);
        if (continuePanic != 0)
        {
            DisableInterrupts();
            Halt();
        }

        Interrupts::BroadcastPanicIpi();
        panicOutputs = AcquirePanicOutputs(AcquireOutputLockAttempts);
        for (size_t i = 0; i < panicOutputs.Size(); i++)
        {
            if (panicOutputs[i]->BeginPanic != nullptr)
                panicOutputs[i]->BeginPanic();
        }

        PrintPanicHeader();
    }

    static void PrintTrace(uintptr_t start)
    {
        PanicPrint("Call stack (latest first):\r\n");

        for (size_t i = 0; i < MaxTraceDepth; i++)
        {
            const uintptr_t addr = GetReturnAddr(i, start);
            if (addr == 0)
                break;

            sl::StringSpan symbolRepo {};
            auto symbol = SymbolFromAddr(addr, SymbolFlag::Public | SymbolFlag::Private, &symbolRepo);
            sl::StringSpan symbolName = "unknown";
            size_t offset = 0;

            if (symbol.HasValue())
            {
                symbolName = symbol->name;
                offset = addr - symbol->base;
            }
            else
                symbolRepo = "unknown";

            const int repoNameLen = sl::Min(MaxProgramNameLen, (int)symbolRepo.Size());
            const int symNameLen = sl::Min(MaxSymbolNameLen, (int)symbolName.Size());
            PanicPrint(TraceFrameFormatStr, i, addr, repoNameLen, symbolRepo.Begin(), symNameLen, 
                symbolName.Begin(), offset);
        }
    }

    static void PrintCoreInfo()
    {
        if (!CoreLocalAvailable())
        {
            PanicPrint("Core-local info not available.\r\n");
            return;
        }

        PanicPrint(CoreFormatStr, CoreLocal().id, (unsigned)CoreLocal().runLevel,
            Tasking::GetRunLevelName(CoreLocal().runLevel), CoreLocal()[LocalPtr::Log]);
    }

    static void PrintProgramInfo()
    {
        if (!CoreLocalAvailable() || CoreLocal()[LocalPtr::Thread] == nullptr)
        {
            PanicPrint("Thread-local info not available.\r\n");
            return;
        }

        auto& thread = Tasking::Thread::Current();
        auto& proc = thread.Parent();
        auto threadName = thread.Name();
        if (threadName.Empty())
            threadName = "<unknown>";
        auto procName = proc.Name();
        if (procName.Empty())
            procName = "<unknown>";

        auto shadow = Drivers::DriverManager::Global().GetShadow();
        auto shadowName = shadow.Valid() ? shadow->manifest->friendlyName.Span() : "n/a";

        PanicPrint(ProgramFormatStr, proc.Id(), thread.Id(), (int)threadName.Size(), threadName.Begin(),
            (int)procName.Size(), procName.Begin(), (int)shadowName.Size(), shadowName.Begin());
    }

    static void EndPanic()
    {
        PanicPrint("\r\n");
        PrintCoreInfo();
        PrintProgramInfo();

        PanicPrint(ResetStr);
        Halt();
        __builtin_unreachable();
    }

    void PanicWithException(Tasking::ProgramException ex, uintptr_t traceStart)
    {
        BeginPanic();
        PanicPrint(ExceptFormatStr, ExceptionStrs[(size_t)ex.type], ex.stack, ex.flags, ex.special);

        sl::StringSpan pcProgramName {};
        auto pcSymbol = SymbolFromAddr(ex.instruction, SymbolFlag::Public | SymbolFlag::Private, 
            &pcProgramName);

        sl::StringSpan pcName = "unknown";
        size_t pcOffset = 0;
        if (pcSymbol.HasValue())
        {
            pcName = pcSymbol->name;
            pcOffset = ex.instruction - pcSymbol->base;
        }
        else
            pcProgramName = {};

        const int programNameLen = sl::Min(MaxProgramNameLen, (int)pcProgramName.Size());
        const int pcNameLen = sl::Min(MaxSymbolNameLen, (int)pcName.Size());
        PanicPrint("PC=0x%lx %.*s!%.*s+0x%lx\r\n", ex.instruction, programNameLen,
            pcProgramName.Begin(), pcNameLen, pcName.Begin(), pcOffset);

        PrintTrace(traceStart);
        EndPanic();
    }

    void Panic(sl::StringSpan reason)
    {
        BeginPanic();

        PanicPrint(reason.Begin());
        PanicPrint("\r\n");
        PrintTrace(0);
        EndPanic();
    }
}
