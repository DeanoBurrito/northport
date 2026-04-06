#include <private/Process.hpp>

namespace Npk
{
    void SessionDtor(void* obj)
    {
        NPK_UNREACHABLE(); (void)obj;
    }

    void JobDtor(void* obj)
    {
        NPK_UNREACHABLE(); (void)obj;
    }

    NpkStatus CreateSession(Session** sesh)
    {
        if (sesh == nullptr)
            return NpkStatus::InvalidArg;

        //TODO: what we should be doing here is creating a directory
        //called `session-X` (CreateObjectWithId()) with a child of type
        //session, which is the session object we return from this function.
        //We'll also want to optionally return the directory object to make
        //the caller's life easier, since they may want to add processes to it.
        //the idea is we'll end up with an obejct tree looking something like:
        //- sessions (Directory)
        //  - session-0 (Directory)
        //      - session (Session)
        //      - processes (Directory)
        //          - process-0 (Directory)
        //              - process (Process)
        //              - threads (Directory)
        //                  - thread-1 (Thread)
        //                  - thread-2 (Thread)
        //                  - thread-3 (Thread)
        //          - process-1 (Directory)
        //              - process (Process)
        //              - threads (Directory)
        //                  - thread-1 (Thread)
        //          - process-2 (Directory)
        //              - process (Process)
        //              - threads (Directory)
        //                  - thread-1 (Thread)
        //"sessions/session-0/processes/process-1/threads/thread-1"

        NsObject* ptr;
        auto result = CreateObjectWithId(&ptr, NsObjType::Session, {},
            "session", 0, 0);
        if (result != NpkStatus::Success)
            return result;

        auto* seshPtr = reinterpret_cast<Session*>(ptr);
        ResetMutex(&seshPtr->jobsMutex, 1);

        *sesh = seshPtr;
        return NpkStatus::Success;
    }

    NpkStatus CreateJob(Job** job, Session& parent)
    {
        if (job == nullptr)
            return NpkStatus::InvalidArg;

        if (!RefObject(parent.nsObj))
            return NpkStatus::ObjRefFailed;

        NsObject* ptr;
        auto result = CreateObjectWithId(&ptr, NsObjType::Job, {}, "job", 0, 0);
        if (result != NpkStatus::Success)
        {
            UnrefSession(parent);

            return result;
        }

        auto* jobPtr = reinterpret_cast<Job*>(ptr);
        ResetMutex(&jobPtr->processesMutex, 1);

        if (!AcquireMutex(&parent.jobsMutex, sl::NoTimeout))
        {
            UnrefSession(parent);
            UnrefJob(*jobPtr);

            return NpkStatus::LockAcquireFailed;
        }
        parent.jobs.PushBack(jobPtr);
        jobPtr->owningSession = &parent;
        //NOTE: we incremented the parent's refcount earlier, from here onwards
        //we treat `jobPtr->owningSession` as a refcounted pointer.
        ReleaseMutex(&parent.jobsMutex);

        *job = jobPtr;
        return NpkStatus::Success;
    }

    void UnrefSession(Session& sesh)
    {
        UnrefObject(sesh.nsObj);
    }
    static_assert(offsetof(Session, nsObj) == 0);

    void UnrefJob(Job& job)
    {
        UnrefObject(job.nsObj);
    }
    static_assert(offsetof(Job, nsObj) == 0);
}
