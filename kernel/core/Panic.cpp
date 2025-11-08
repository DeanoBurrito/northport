#include <CorePrivate.hpp>
#include <Debugger.hpp>
#include <NanoPrintf.hpp>
#include <Maths.hpp>

//NOTE: this is a bit dodge, but this gives us access to the build info
#include <EntryPrivate.hpp>

namespace Npk
{
    constexpr size_t PanicPrintBufferSize = 80;
    constexpr size_t CallstackDepth = 16;

    static LogSinkList panicOutputs;

    static size_t PanicPrint(const char* format, ...)
    {
        char buffer[PanicPrintBufferSize];
        va_list args;
        va_start(args, format);

        size_t bufferLen = npf_vsnprintf(buffer, PanicPrintBufferSize, 
            format, args);
        bufferLen = sl::Min(bufferLen, PanicPrintBufferSize);
        va_end(args);

        for (auto it = panicOutputs.Begin(); it != panicOutputs.End(); ++it)
        {
            if (it->Write == nullptr)
                continue; //not sure how this happened, but too late to worry
                          //about dealing with it.

            LogSinkMessage msg {};
            msg.text = { buffer, bufferLen };

            it->Write(msg);
        }

        return bufferLen;
    }

    static void DumpHeader()
    {} //TODO: fancy ascii banner
    
    static void DumpBuildInfo()
    {
        PanicPrint("Build info:\r\n");
        PanicPrint("commit: %s%s", gitHash, gitDirty ? "-dirty" : "");
        PanicPrint("\r\n");
        PanicPrint("kernel version: %u.%u.%u", versionMajor, versionMinor,
            versionRev);
        PanicPrint("\r\n");
        PanicPrint("compiler flags: %s", compileFlags);
        PanicPrint("\r\n\r\n");
    }

    static void DumpCpuInfo()
    {
        PanicPrint("CPU info:\r\n");
        HwDumpPanicInfo(PanicPrintBufferSize, PanicPrint);
        PanicPrint("\r\n");
    }

    static void DumpBytesAt(uintptr_t addr, size_t count)
    {
        uint8_t bytes[4];
        size_t xPos = 0;

        for (size_t i = 0; i < count; i += 4)
        {
            void* src = reinterpret_cast<void*>(addr + i);

            if (xPos == 0)
                xPos = PanicPrint("%p: ", src);

            size_t copied = UnsafeMemCopy(bytes, src, sl::Min(4ul, count - i));

            for (size_t j = 0; j < copied; j++)
                PanicPrint("%02x ", bytes[j]);
            for (size_t j = copied; j < 4; j++)
                PanicPrint("?? ");
            xPos += 13; //3 chars per byte, 4 bytes, 1 trailing space

            if (xPos + 11 > PanicPrintBufferSize)
            {
                xPos = 0;
                PanicPrint("\r\n");
            }
            else if ((i % 4) == 0)
            {
                xPos += 2;
                PanicPrint("  ");
            }
        }

        PanicPrint("\r\n");
        if (xPos != 0)
            PanicPrint("\r\n");
    }

    static void DumpWordsAt(uintptr_t addr, size_t count)
    {
        const size_t wordWidth = npf_snprintf(nullptr, 0, "0x%0tx", addr);

        uintptr_t word;
        size_t xPos = 0;

        addr = sl::AlignDown(addr, (sizeof(addr) * 8) - 1);
        for (size_t i = 0; i < count; i++)
        {
            void* src = reinterpret_cast<void*>(addr + i * sizeof(uintptr_t));

            if (xPos == 0)
                xPos = PanicPrint("%p: ", src);

            size_t copied = UnsafeMemCopy(&word, src, sizeof(word));

            if (copied != sizeof(word))
                PanicPrint("??%*.0s ", (int)wordWidth - 2, nullptr);
            else
                PanicPrint("0x%0*tx ", (int)wordWidth - 2, word);
            xPos += wordWidth + 1;

            if (xPos + wordWidth > PanicPrintBufferSize)
            {
                xPos = 0;
                PanicPrint("\r\n");
            }
        }

        PanicPrint("\r\n");
        if (xPos != 0)
            PanicPrint("\r\n");
    }

    static void DumpCallstack(uintptr_t start)
    {
        PanicPrint("Call stack (latest first):\r\n");

        uintptr_t stack[CallstackDepth];
        const size_t stackSize = GetCallstack(stack, start);

        //TODO: include current PC as frame 0
        for (size_t i = 0; i < stackSize; i++)
        {
            if (stack[i] == 0)
                break;

            //TODO: symbol and module name resolution
            PanicPrint("%02u: 0x%0tx ??+0x0\r\n", i, stack[i]);
        }

        PanicPrint("\r\n");
    }

    [[noreturn]]
    void Panic(sl::StringSpan message, TrapFrame* frame)
    {
        IntrsOff();

        //log that this core is starting the panic sequence, in case another 
        //core also panics and beats us to it. This can happen by the cores
        //racing to call `FreezeAllCpus()` in the loop below. Whichever cpu
        //calls it first will be the one to complete the panic sequence and
        //its error message will be displayed.
        //So to assist with debugging, we also log the panic message before
        //that point.
        Log("Panic pending on cpu %zu: %.*s, frame=%p", LogLevel::Error, 
            MyCoreId(), (int)message.Size(), message.Begin(), frame);

        FreezeAllCpus(true);
        //Only one cpu will be executing past this point

        Private::AcquirePanicOutputs(panicOutputs);
        for (auto it = panicOutputs.Begin(); it != panicOutputs.End(); ++it)
            it->BeginPanic();
        
        DumpHeader();

        PanicPrint("\r\n");
        PanicPrint("%.*s", (int)message.Size(), message.Begin());
        PanicPrint("\r\n\r\n");

        DumpBuildInfo();
        DumpCpuInfo();
        if (frame != nullptr)
            DumpCallstack(GetTrapBasePtr(frame));
        else
            DumpCallstack(0);
        
        if (frame != nullptr)
        {
            PanicPrint("Bytes at program counter:\r\n");
            DumpBytesAt(GetTrapReturnAddr(frame), 64);
            PanicPrint("Stack words:\r\n");
            DumpWordsAt(GetTrapStackPtr(frame), 8);
        }

        ConnectDebugger();
        DebugBreakpoint();

        while (true)
            WaitForIntr();
        SL_UNREACHABLE();
    }
}
