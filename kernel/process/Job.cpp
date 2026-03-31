#include <private/Process.hpp>

namespace Npk
{
    static void SessionDtor(void* obj)
    { NPK_UNREACHABLE(); }

    static void JobDtor(void* obj)
    { NPK_UNREACHABLE(); }

    NpkStatus CreateSession(Session** sesh)
    {
        if (sesh == nullptr)
            return NpkStatus::InvalidArg;

        //TODO: encode session id into name
        void* ptr;
        auto result = CreateObject(&ptr, sizeof(Session), SessionDtor, 
            "session", ProcTreeTag);
        if (result != NpkStatus::Success)
            return result;

        auto* seshPtr = static_cast<Session*>(ptr);
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

        //TODO: encode job id into name
        void* ptr;
        auto result = CreateObject(&ptr, sizeof(Job), JobDtor, 
            "job", ProcTreeTag);
        if (result != NpkStatus::Success)
            return result;

        auto* jobPtr = static_cast<Job*>(ptr);
        ResetMutex(&jobPtr->processesMutex, 1);

        if (!AcquireMutex(&parent.jobsMutex, sl::NoTimeout))
        {
            UnrefSession(parent);
            UnrefObject(jobPtr->nsObj);

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

    VmSpace& GetProcessVmSpace(Process& proc)
    { NPK_UNREACHABLE(); }
}
