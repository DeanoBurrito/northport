#include <Log.h>
#include <stdarg.h>
#include <Format.h>

/*
    This file contains Logf() and associated functions. It's separated as the other log functions
    can be called before pmm is initialized (debugcon is always available in a virtual machine),
    where as we wouldnt be able to use the string format functions (they required the heap, which needs vmm & pmm).
    This just avoids polluting Log.cpp will higher level functions that we might use to accidentally
    try to allocate stuff with.
*/
namespace Kernel
{
    extern bool fullLoggingAvail;

    void Logf(const char* formatStr, LogSeverity level, ...)
    {
        if (!fullLoggingAvail)
            return; //obi-wan: don't try it!

        //unfortunately we can't reuse the buffer here (not without serious hacks), so we'll have to eat the extra copy :(
        string format(formatStr);
        
        va_list varArgs;
        va_start(varArgs, level);
        string formatted = sl::FormatToStringV(format, varArgs);
        va_end(varArgs);

        Log(formatted.C_Str(), level);
    }
}
