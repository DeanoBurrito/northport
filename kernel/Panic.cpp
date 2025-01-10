#include <Panic.h>
#include <arch/Misc.h>
#include <arch/Interrupts.h>
#include <core/Log.h>
#include <core/Smp.h>
#include <services/Program.h>
#include <services/SymbolStore.h>
#include <Atomic.h>
#include <Maths.h>
#include <NanoPrintf.h>
#include <Compiler.h>

namespace Npk
{
    constexpr const char* ExceptFormatStr = "Unhandled exception: %s, stack=0x%tx, flags=0x%x, s=0x%tx\r\n";
    constexpr const char* ExceptPcStr = "PC=0x%tx %.*s!%.*s +0x%lx\r\n";
    constexpr const char* ExceptMemoryStr = "   0x%0tx: %02x %02x %02x %02x   %02x %02x %02x %02x   %02x %02x %02x %02x   %02x %02x %02x %02x\r\n";
    constexpr const char* ExceptStackStr = "   0x%0tx: 0x%016tx 0x%016tx\r\n";
    constexpr const char* CoreFormatStr = "Core %tu: runLevel %u (%s), thread=%p, logs=%p\r\n";
    constexpr const char* ProgramFormatStr = "Thread %zu.%zu: name=%.*s, procName=%.*s driverShadow=%.*s\r\n";
    constexpr const char* TraceFrameFormatStr = "%3zu: 0x%016tx %.*s!%.*s +0x%lx\r\n";
    constexpr const char* ResetStr = "\r\nSystem has halted indefinitely, manual reset required.\r\n";

    constexpr size_t MaxPanicLogLen = 128;
    constexpr size_t MaxTraceDepth = 16;
    constexpr int MaxProgramNameLen = 16;
    constexpr int MaxSymbolNameLen = 52;
    constexpr size_t PrintBytesCount = 64;
    constexpr size_t PrintWordsCount = 8;
    constexpr size_t AcquireOutputLockAttempts = 4321'0000;

    static_assert(PrintBytesCount % 16 == 0);
    static_assert(PrintWordsCount % 2 == 0);

    sl::Atomic<size_t> panicFlag;
    using PanicOutputs = const sl::Span<const Core::LogOutput*>;

    SL_PRINTF_FUNC(2, 3)
    static void PanicPrint(PanicOutputs outputs, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const size_t length = npf_vsnprintf(nullptr, 0, format, args) + 1;
        va_end(args);

        char buffer[MaxPanicLogLen];
        va_start(args, format);
        npf_vsnprintf(buffer, MaxPanicLogLen, format, args);
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

    static void PrintBytesAt(PanicOutputs outputs, uintptr_t addr)
    {
        uint8_t buffer[PrintBytesCount];
        const size_t copied = UnsafeCopy(buffer, reinterpret_cast<void*>(addr), PrintBytesCount);

        if (copied == 0)
        {
            PanicPrint(outputs, "   <cannot safely access memory>\r\n");
            return;
        }

        for (size_t i = 0; i < copied; i += 16)
        {
            PanicPrint(outputs, ExceptMemoryStr, addr + i,
                buffer[i + 0], buffer[i + 1], buffer[i + 2], buffer[i + 3],
                buffer[i + 4], buffer[i + 5], buffer[i + 6], buffer[i + 7],
                buffer[i + 8], buffer[i + 9], buffer[i + 10], buffer[i + 11],
                buffer[i + 12], buffer[i + 13], buffer[i + 14], buffer[i + 15]);
        }
    }

    static void PrintWordsAt(PanicOutputs outputs, uintptr_t addr)
    {
        uintptr_t buffer[PrintWordsCount];
        const size_t copied = UnsafeCopy(buffer, reinterpret_cast<void*>(addr), 
            PrintWordsCount * sizeof(uintptr_t)) / sizeof(uintptr_t);

        if (copied == 0)
        {
            PanicPrint(outputs, "   <cannot safely access memory>\r\n");
            return;
        }

        for (size_t i = 0; i < copied; i++)
        {
            PanicPrint(outputs, ExceptStackStr, addr + (i * sizeof(uintptr_t)), 
                buffer[i], buffer[i + 1]);
        }
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

            auto symbolInfo = Services::FindSymbol(callstack[i]);
            sl::StringSpan symbolName = symbolInfo.HasValue() ? symbolInfo->info->name : "????";
            sl::StringSpan repoName = symbolInfo.HasValue() ? symbolInfo->repo->name : "?";
            const size_t offset = symbolInfo.HasValue() ? callstack[i] - symbolInfo->info->base : 0;

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

        PanicPrint(outputs, CoreFormatStr, CoreLocalId(), (unsigned)CurrentRunLevel(),
            Core::RunLevelName(CurrentRunLevel()), GetLocalPtr(SubsysPtr::Thread),
            GetLocalPtr(SubsysPtr::Logs));
    }

    static void PrintProgramInfo(PanicOutputs outputs)
    {
        (void)outputs;
        //TODO: print program info
    }

    static void EndPanic(PanicOutputs outputs, uintptr_t traceStart)
    {
        //TODO: print driver load windows
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

        auto pcSymbol = Services::FindSymbol(ex.pc);
        sl::StringSpan repoName = pcSymbol.HasValue() ? pcSymbol->repo->name : "?";
        sl::StringSpan symName = pcSymbol.HasValue() ? pcSymbol->info->name : "????";
        const size_t offset = pcSymbol.HasValue() ? ex.pc - pcSymbol->info->base : 0;
        PanicPrint(outputs, ExceptPcStr, ex.pc, (int)repoName.Size(), repoName.Begin(),
            (int)symName.Size(), symName.Begin(), offset);

        PrintBytesAt(outputs, ex.pc);
        PanicPrint(outputs, "Stack:\r\n");
        PrintWordsAt(outputs, ex.stack);

        if (ex.type == Services::ExceptionType::MemoryAccess 
            && ex.special != ex.stack && ex.special != ex.pc)
        {
            PanicPrint(outputs, "Target:\r\n");
            PrintBytesAt(outputs, ex.special);
        }

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
