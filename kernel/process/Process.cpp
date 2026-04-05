#include <private/Process.hpp>

namespace Npk
{
    void ProcessDtor(void* obj)
    {
        NPK_UNREACHABLE(); (void)obj;
    }

    void ThreadDtor(void* obj)
    {
        NPK_UNREACHABLE(); (void)obj;
    }

    NpkStatus CreateProcess(Process** proc, Job& parent)
    {
        if (proc == nullptr)
            return NpkStatus::InvalidArg;

        if (!RefObject(parent.nsObj))
            return NpkStatus::ObjRefFailed;

        void* ptr;
        auto result = CreateObjectWithId(&ptr, NsObjType::Process, {}, "proc", 
            0, 0);
        if (result != NpkStatus::Success)
        {
            UnrefJob(parent);

            return result;
        }

        auto* procPtr = static_cast<Process*>(ptr);
        ResetMutex(&procPtr->threadsMutex, 1);
        ResetMutex(&procPtr->signalsMutex, 1);

        if (!AcquireMutex(&parent.processesMutex, sl::NoTimeout))
        {
            UnrefJob(parent);
            UnrefProcess(*procPtr);

            return NpkStatus::LockAcquireFailed;
        }
        parent.processes.PushBack(procPtr);
        procPtr->owningJob = &parent;
        //NOTE: we incremented the parent's refcount earlier, from here onwards
        //we treat `procPtr->owningJob` as a refcounted pointer.
        ReleaseMutex(&parent.processesMutex);

        *proc = procPtr;
        return NpkStatus::Success;
    }

    void UnrefProcess(Process& proc)
    {
        UnrefObject(proc.nsObj);
    }
    static_assert(offsetof(Process, nsObj) == 0);

    NpkStatus GetProcessVmSpace(VmSpace** space, Process& proc)
    {
        if (space == nullptr)
            return NpkStatus::InvalidArg;

        *space = &proc.vmSpace;

        return NpkStatus::Success;
    }
}
