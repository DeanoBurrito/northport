#pragma once

#include <Core.hpp>

namespace Npk
{
    constexpr size_t SignalPriorityDefault = -1;

    struct Thread;
    struct Process;
    struct Job;
    struct Session;

    enum class SignalTargetType
    {
        Invalid,
        Thread,
        Process,
        CurrentThread,
        CurrentProcess,
        AllProcesses,
    };

    enum class SignalActionFlag
    {
        NoChildStop,
        OnStack,
        ResetHand,
        Restart,
        SigInfo,
        NoChildWait,
        NoDefer,
    };

    using SignalActionFlags = sl::Flags<SignalActionFlag>;

    struct Credentials
    {
        uint16_t domain;
        uint16_t subdomain;
        uint32_t id;
    };

    NpkStatus CreateSession(Session** sesh);
    NpkStatus CreateJob(Job** job, Session& parent);
    NpkStatus CreateProcess(Process** proc, Job& parent);
    NpkStatus CreateThread(Thread** thread, Process& parent);

    void UnrefSession(Session& sesh);
    void UnrefJob(Job& job);
    void UnrefProcess(Process& proc);
    void UnrefThread(Thread& thread);

    VmSpace& GetProcessVmSpace(Process& proc);

    NpkStatus SendSignal(SignalTargetType type, void* target, uint8_t priority,
        size_t signalId, void* arg);
}
