#include <private/Process.hpp>
#include <Namespace.hpp>

namespace Npk
{
    void InitProcessSubsystem()
    {
        SetObjectTypeInfo(NsObjType::Session, SessionDtor, sizeof(Session), 
            ProcTreeTag);
        SetObjectTypeInfo(NsObjType::Job, JobDtor, sizeof(Job), ProcTreeTag);
        SetObjectTypeInfo(NsObjType::Process, ProcessDtor, sizeof(Process), 
            ProcTreeTag);
        SetObjectTypeInfo(NsObjType::Thread, ThreadDtor, sizeof(Thread), 
            ProcTreeTag);
    }
}
