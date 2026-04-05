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

        void* ptr;
        auto result = CreateObjectWithId(&ptr, NsObjType::Session, {},
            "session", 0, 0);
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

        void* ptr;
        auto result = CreateObjectWithId(&ptr, NsObjType::Job, {}, "job", 0, 0);
        if (result != NpkStatus::Success)
        {
            UnrefSession(parent);

            return result;
        }

        auto* jobPtr = static_cast<Job*>(ptr);
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
