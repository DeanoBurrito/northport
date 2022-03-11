#include <Log.h>
#include <Platform.h>
#include <Cpu.h>
#include <stddef.h>
#include <Panic.h>
#include <Locks.h>

namespace Kernel
{
    bool fullLoggingAvail; //a bit cheeky, but I dont want to advertise this.
    bool logDestsStatus[(unsigned)LogDestination::EnumCount];
    
    char debugconLock;

    void LoggingInitEarly()
    {
        //make sure all logging is disabled at startup
        for (unsigned i = 0; i < (unsigned)LogDestination::EnumCount; i++)
            logDestsStatus[i] = false;

        fullLoggingAvail = false;

        sl::SpinlockRelease(&debugconLock);
    }

    void LoggingInitFull()
    {
        fullLoggingAvail = true;
    }

    bool IsLogDestinationEnabled(LogDestination dest)
    {
        if ((unsigned)dest < (unsigned)LogDestination::EnumCount)
            return logDestsStatus[(unsigned)dest];
        return false;
    }

    void EnableLogDestinaton(LogDestination dest, bool enabled)
    {
        if ((unsigned)dest < (unsigned)LogDestination::EnumCount)
            logDestsStatus[(unsigned)dest] = enabled;
    }
    
    void Log(const char* message, LogSeverity level)
    {   
        const char* headerStr = "\0";
#ifdef NORTHPORT_DEBUG_LOGGING_COLOUR_LEVELS
        switch (level) 
            {
            case LogSeverity::Info:
                headerStr = "\033[97m[Info]\033[39m ";
                break;
            case LogSeverity::Warning:
                headerStr = "\033[93m[Warn]\033[39m ";
                break;;
            case LogSeverity::Error:
                headerStr = "\033[91m[Error]\033[39m ";
                break;
            case LogSeverity::Fatal:
                headerStr = "\033[31m[Fatal]\033[39m ";
                break;
            case LogSeverity::Verbose:
                headerStr = "\033[90m[Verbose]\033[39m ";
                break;
            default:
                break;
            }
#else
        switch (level) 
        {
        case LogSeverity::Info:
            headerStr = "[Info] ";
            break;
        case LogSeverity::Warning:
            headerStr = "[Warn] ";
            break;;
        case LogSeverity::Error:
            headerStr = "[Error] ";
            break;
        case LogSeverity::Fatal:
            headerStr = "[Fatal] ";
            break;
        case LogSeverity::Verbose:
            headerStr = "[Verbose] ";
            break;
        default:
            break;
        }
#endif
        
        for (unsigned i = 0; i < (unsigned)LogDestination::EnumCount; i++)
        {
            if (!logDestsStatus[i])
                continue;
            
            switch ((LogDestination)i)
            {
            case LogDestination::DebugCon:
                {
                    InterruptLock intLock;
                    sl::ScopedSpinlock scopeLock(&debugconLock);

                    for (size_t index = 0; headerStr[index] != 0; index++)
                        CPU::PortWrite8(PORT_DEBUGCON, headerStr[index]);
                    
                    for (size_t index = 0; message[index] != 0; index++)
                        CPU::PortWrite8(PORT_DEBUGCON, message[index]);

                    CPU::PortWrite8(PORT_DEBUGCON, '\n');
                    CPU::PortWrite8(PORT_DEBUGCON, '\r');
                    break;
                }

            case LogDestination::FramebufferOverwrite:
                break;

            default:
                break;
            }
        }

        if (level == LogSeverity::Fatal)
            Panic(message);
    }
}
