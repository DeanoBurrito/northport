#pragma once

#include <Process.hpp>
#include <Namespace.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag ProcTreeTag = NPK_MAKE_HEAP_TAG("PsTr");

    struct SignalSet
    {
        uint64_t bits;
    };
    
    struct SignalAction
    {
        sl::ListHook hook;

        //0 = perform default action/invalid, 1 = ignore, 2-7 = reserved,
        //all other values indicate a valid user pc for the signal handler
        //to jump to.
        uintptr_t pc;
        uintptr_t stack; //0 = invalid, use thread's existing stack
        SignalSet maskedSignals;
        SignalActionFlags flags;
    };

    using SignalActionList = sl::List<SignalAction, &SignalAction::hook>;

    struct PendingSignal
    {
        sl::ListHook hook;

        size_t id;
        void* arg;
        uint8_t priority;
    };

    using PendingSignalList = sl::List<PendingSignal, &PendingSignal::hook>;

    struct Thread
    {
        NsObject nsObj;

        ThreadContext* context;
        Process* owningProcess;
        sl::ListHook processListHook;
    };

    using ThreadList = sl::List<Thread, &Thread::processListHook>;

    struct Process
    {
        NsObject nsObj;

        Mutex threadsMutex;
        ThreadList threads;
        Job* owningJob;

        sl::ListHook jobListHook;

        Mutex signalsMutex;
        SignalSet maskedSignalSet;
        SignalSet pendingSignalSet;
        SignalSet actionsSignalSet;
        PendingSignalList signalsPending;
        SignalActionList signalActions;
    };

    using ProcessList = sl::List<Process, &Process::jobListHook>;

    struct Job
    {
        NsObject nsObj;

        Mutex processesMutex;
        ProcessList processes;
        Session* owningSession;

        sl::ListHook sessionListHook;
    };

    using JobList = sl::List<Job, &Job::sessionListHook>;

    struct Session
    {
        NsObject nsObj;

        Mutex jobsMutex;
        JobList jobs;
        Credentials credentials;
    };
}
