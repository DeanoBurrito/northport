#include <Namespace.hpp>
#include <Loader.hpp>
#include <Process.hpp>

namespace Npk
{
    NpkStatus LoadInitProgram()
    {
        auto programName = ReadConfigString("npk.init.program_name", ""_span);
        if (programName.Size() == 0)
        {
            Log("Unable to determine init program", LogLevel::Error);

            return NpkStatus::BadConfig;
        }

        auto programArgs = ReadConfigString("npk.init.args", "");
        Log("Will load init program: %.*s, args=%.*s", LogLevel::Info,
            (int)programName.Size(), programName.Begin(),
            (int)programArgs.Size(), programArgs.Begin());

        NsObject* programObj;
        auto result = FindObject(&programObj, nullptr, programName);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return result;
        }

        Session* initSession;
        result = CreateSession(&initSession);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return result;
        }

        Job* initJob;
        result = CreateJob(&initJob, *initSession);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            UnrefSession(*initSession);

            return result;
        }

        Process* initProc;
        result = CreateProcess(&initProc, *initJob);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            UnrefSession(*initSession);
            UnrefJob(*initJob);

            return result;
        }

        VmSpace& space = GetProcessVmSpace(*initProc);
        result = LoadProgramFromNsObj(space, *programObj);
        
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            UnrefSession(*initSession);
            UnrefJob(*initJob);
            UnrefProcess(*initProc);

            return result;
        }
        
        //TODO: setup thread context, stack and FDs to bringup resources.

        return NpkStatus::Unsupported;
        //return result;
    }
}
