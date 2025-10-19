#include <Core.hpp>
#include <Debugger.hpp>
#include <NanoPrintf.hpp>
#include <Maths.hpp>

//NOTE: this is a bit dodge, but this gives us access to the build info
#include <EntryPrivate.hpp>

namespace Npk
{
    constexpr size_t PanicPrintBufferSize = 80;
    constexpr size_t CallstackDepth = 16;

    static size_t PanicPrint(const char* format, ...)
    {
        char buffer[PanicPrintBufferSize];
        va_list args;
        va_start(args, format);

        const size_t bufferLen = npf_vsnprintf(buffer, PanicPrintBufferSize, 
            format, args);
        va_end(args);

        //TODO: panic ouputs
        return bufferLen;
    }

    static void DumpHeader()
    {} //TODO: fancy ascii banner
    
    static void DumpBuildInfo()
    {
        PanicPrint("Build info:\n");
        PanicPrint("commit: %s%s", gitHash, gitDirty ? "-dirty" : "");
        PanicPrint("kernel version: %u.%u.%u", versionMajor, versionMinor,
            versionRev);
        PanicPrint("compiler flags: %s", compileFlags);
    }

    static void DumpCpuInfo()
    {
        PanicPrint("CPU info:\n");
        HwDumpPanicInfo(PanicPrintBufferSize, PanicPrint);
    }

    static void DumpBytesAt(uintptr_t addr, size_t count)
    {
        uint8_t bytes[4];
        size_t xPos = 0;

        for (size_t i = 0; i < count; i += 4)
        {
            void* src = reinterpret_cast<void*>(addr + i * 4);

            if (xPos == 0)
                xPos = PanicPrint("%p: ", src);

            size_t copied = UnsafeMemCopy(bytes, src, sl::Min(4ul, count - i));

            for (size_t j = 0; j < copied; j++)
                PanicPrint("%02x ", bytes[i]);
            for (size_t j = copied; j < 4; j++)
                PanicPrint("?? ");
            xPos += 13; //3 chars per byte, 4 bytes, 1 trailing space

            if (xPos + 11 > PanicPrintBufferSize)
            {
                xPos = 0;
                PanicPrint("\n");
            }
        }

        PanicPrint("\n");
        if (xPos != 0)
            PanicPrint("\n");
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
                PanicPrint("??");
            else
                PanicPrint("0x%0tx", word);
            xPos += wordWidth + 1;

            if (xPos + wordWidth > PanicPrintBufferSize)
            {
                xPos = 0;
                PanicPrint("\n");
            }
        }

        PanicPrint("\n");
        if (xPos != 0)
            PanicPrint("\n");
    }

    static void DumpCallstack(uintptr_t start)
    {
        PanicPrint("Call stack (latest first):\n");

        uintptr_t stack[CallstackDepth];
        const size_t stackSize = GetCallstack(stack, start);

        //TODO: include current PC as frame 0
        for (size_t i = 0; i < stackSize; i++)
        {
            if (stack[i] == 0)
                break;

            //TODO: symbol and module name resolution
            PanicPrint("%02u: 0x%0tx ??+0x0\n", i, stack[i]);
        }

        PanicPrint("\n");
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

        while (FreezeAllCpus() == 0)
        {
            IntrsOn();
            for (size_t i = 0; i < 1234; i++)
                asm volatile("");
            IntrsOff();
        }
        //Only one cpu will be executing past this point
        
        //TODO: try to detach any storage devices (flushing caches where needed)

        DumpHeader();
        DumpBuildInfo();
        DumpCpuInfo();
        if (frame != nullptr)
            DumpCallstack(GetTrapBasePtr(frame));
        else
            DumpCallstack(0);
        
        if (frame != nullptr)
        {
            PanicPrint("Bytes at program counter:\n");
            DumpBytesAt(GetTrapReturnAddr(frame), 64);
            PanicPrint("Stack words:\n");
            DumpWordsAt(GetTrapStackPtr(frame), 8);
        }

        ConnectDebugger();
        DebugBreakpoint();

        while (true)
            WaitForIntr();
        SL_UNREACHABLE();
    }
}
