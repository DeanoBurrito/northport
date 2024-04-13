#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Span.h>

namespace Npk::Debug
{
    enum class LogLevel : size_t
    {
        Fatal = 0,
        Error = 1,
        Warning = 2,
        Info = 3,
        Verbose = 4,
        Debug = 5,
    };

    [[gnu::format(printf, 1, 3)]]
    void Log(const char* str, LogLevel level, ...);

    struct LogOutput
    {
        void (*Write)(sl::StringSpan message);
        void (*BeginPanic)();
    };


    void InitCoreLogBuffers();
    void AddLogOutput(LogOutput* output);
    sl::Span<LogOutput*> AcquirePanicOutputs(size_t tryLockCount);
}

using Npk::Debug::LogLevel;
using Npk::Debug::Log;

/* Notes on the following debug macros. ASSERT and VALIDATE both help ensure that some
 * pre-condition is true. They differ in how they handle failure: ASSERT is considered a critical
 * assumption, and so if an ASSERT fails it causes a kernel panic. If a VALIDATE macro fails, it
 * returns from the current function, optionally with a return value. Both macros will emit a 
 * message to the kernel log on failure.
 *
 * The versions of these macros have a version with a '_' after the macro name. These versions
 * dont supply a custom error message, instead they print the code for the condition. 
 * Useful if you cant think of decent error messages to write, just show the code instead.
 *
 * ASSERT_UNREACHABLE causes a kernel panic if it's ever reached, useful for stubbing functions
 * or testing that a loop exited in the way you thought.
 */
#define ASSERT(cond, msg) \
{ \
    if (!(cond)) \
        Log("Assert failed (%s:%u): %s", LogLevel::Fatal, __FILE__, __LINE__, (msg)); \
}

#define ASSERT_(cond) \
{ \
    if (!(cond)) \
        Log("Assert failed (%s:%u): " #cond, LogLevel::Fatal, __FILE__, __LINE__); \
}

#define ASSERT_UNREACHABLE() \
{ \
    Log("Assert failed (%s:%u): ASSERT_UNREACHABLE reached.", LogLevel::Fatal, __FILE__, __LINE__); \
    __builtin_unreachable(); \
}

#define VALIDATE(cond, retval, msg) \
{ \
    if (!(cond)) \
    { \
        Log("Check failed (%s:%u): %s", LogLevel::Error, __FILE__, __LINE__, (msg)); \
        return retval; \
    } \
}

#define VALIDATE_(cond, retval) \
{ \
    if (!(cond)) \
    { \
        Log("Check failed (%s:%u): " #cond, LogLevel::Error, __FILE__, __LINE__); \
        return retval; \
    } \
}
