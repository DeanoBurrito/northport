#include <Panic.h>
#include <arch/Misc.h>
#include <arch/Interrupts.h>
#include <core/Log.h>
#include <core/Smp.h>
#include <interfaces/intra/Compiler.h>
#include <services/Program.h>
#include <Atomic.h>
#include <Maths.h>
#include <NanoPrintf.h>

namespace Npk
{
    constexpr const char* ExceptFormatStr = "Unhandled exception: %s, stack=0x%tx, flags=0x%x, s=0x%tx\r\n";
    constexpr const char* CoreFormatStr = "Core %tu: runLevel %u (%s), thread=%p, logs=%p\r\n";
    constexpr const char* ProgramFormatStr = "Thread %zu.%zu: name=%.*s, procName=%.*s driverShadow=%.*s\r\n";
    constexpr const char* TraceFrameFormatStr = "%3zu: 0x%016lx %.*s!%.*s+0x%lx\r\n";
    constexpr const char* ResetStr = "\r\nSystem has halted indefinitely, manual reset required.\r\n";
    constexpr size_t MaxTraceDepth = 16;
    constexpr int MaxProgramNameLen = 16;
    constexpr int MaxSymbolNameLen = 52;
    constexpr size_t AcquireOutputLockAttempts = 4321'0000;

    sl::Atomic<size_t> panicFlag;
    using PanicOutputs = const sl::Span<const Core::LogOutput*>;

    PRINTF_FUNCTION(2, 3)
    static void PanicPrint(PanicOutputs outputs, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const size_t length = npf_vsnprintf(nullptr, 0, format, args) + 1;
        va_end(args);

        char buffer[length];
        va_start(args, format);
        npf_vsnprintf(buffer, length, format, args);
        va_end(args);

        for (size_t i = 0; i < outputs.Size(); i++)
        {
            if (outputs[i]->Write != nullptr)
                outputs[i]->Write({ buffer, length });
        }
    }

    static void PrintPanicHeader(PanicOutputs outputs)
    {
        PanicPrint(outputs, "\r\n");
        PanicPrint(outputs, "       )\r\n");
        PanicPrint(outputs, "    ( /(                        (\r\n");
        PanicPrint(outputs, "    )\\())  (   (             (  )\\             )        (\r\n");
        PanicPrint(outputs, "   ((_)\\  ))\\  )(    (      ))\\((_)  `  )   ( /(   (    )\\   (\r\n");
        PanicPrint(outputs, "  (_ ((_)/((_)(()\\   )\\ )  /((_)_    /(/(   )(_))  )\\ )((_)  )\\\r\n");
        PanicPrint(outputs, "  | |/ /(_))   ((_) _(_/( (_)) | |  ((_)_\\ ((_)_  _(_/( (_) ((_)\r\n");
        PanicPrint(outputs, "  | ' < / -_) | '_|| ' \\))/ -_)| |  | '_ \\)/ _` || ' \\))| |/ _|\r\n");
        PanicPrint(outputs, "  |_|\\_\\\\___| |_|  |_||_| \\___||_|  | .__/ \\__,_||_||_| |_|\\__|\r\n");
        PanicPrint(outputs, "                                    |_|\r\n");
    }

    static PanicOutputs BeginPanic()
    {
        DisableInterrupts();
        const size_t continuePanic = panicFlag.FetchAdd(1);
        if (continuePanic != 0)
            Halt(); //TODO: would be nice to signal a recursive panic somehow

        Core::PanicAllCores();
        PanicOutputs outputs = Core::AcquirePanicOutputs(AcquireOutputLockAttempts);
        for (size_t i = 0; i < outputs.Size(); i++)
        {
            if (outputs[i]->BeginPanic != nullptr)
                outputs[i]->BeginPanic();
        }

        PrintPanicHeader(outputs);
        return outputs;
    }

    static void PrintCallstack(PanicOutputs outputs, uintptr_t start)
    {
        PanicPrint(outputs, "Call stack (latest first):\r\n");

        uintptr_t callstack[MaxTraceDepth];
        const size_t callstackDepth = sl::Min(GetCallstack({ callstack, MaxTraceDepth }, start), MaxTraceDepth);

        for (size_t i = 0; i < callstackDepth; i++)
        {
            if (callstack[i] == 0)
                break;

            //TODO: symbol lookup
            sl::StringSpan repoName = "unknown";
            sl::StringSpan symbolName = "unknown";
            size_t offset = 0;

            const int repoNameLen = sl::Min(MaxProgramNameLen, (int)repoName.Size());
            const int symNameLen = sl::Min(MaxSymbolNameLen, (int)symbolName.Size());
            PanicPrint(outputs, TraceFrameFormatStr, i, callstack[i], repoNameLen, repoName.Begin(),
                symNameLen, symbolName.Begin(), offset);
        }
    }

    static void PrintCoreInfo(PanicOutputs outputs)
    {
        if (!CoreLocalAvailable())
        {
            PanicPrint(outputs, "Core-local info not available.\r\n");
            return;
        }

        PanicPrint(outputs, CoreFormatStr, CoreLocal().id, (unsigned)CoreLocal().runLevel,
            Core::RunLevelName(CoreLocal().runLevel), CoreLocal()[LocalPtr::Thread],
            CoreLocal()[LocalPtr::Logs]);
    }

    static void PrintProgramInfo(PanicOutputs outputs)
    {
        //TODO: print program info
    }

    static void EndPanic(PanicOutputs outputs, uintptr_t traceStart)
    {
        PrintCallstack(outputs, traceStart);
        PrintCoreInfo(outputs);
        PrintProgramInfo(outputs);

        PanicPrint(outputs, ResetStr);
        Halt();
        __builtin_unreachable();
    }

    void PanicWithException(Services::ProgramException ex, uintptr_t traceStart)
    { 
        PanicOutputs outputs = BeginPanic();

        PanicPrint(outputs, ExceptFormatStr, Services::ExceptionName(ex.type),
            ex.stack, ex.flags, ex.special);

        //TODO: PC symbol lookup
        EndPanic(outputs, traceStart);
    }

    void PanicWithString(sl::StringSpan reason)
    { 
        PanicOutputs outputs = BeginPanic();

        PanicPrint(outputs, "%.*s", (int)reason.Size(), reason.Begin());
        PanicPrint(outputs, "\r\n");

        EndPanic(outputs, 0);
    }
}
