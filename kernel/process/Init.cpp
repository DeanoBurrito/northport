#include <private/Process.hpp>
#include <Namespace.hpp>

namespace Npk
{
    void InitProcessSubsystem()
    {
        SetNsObjTypeInfo(NsObjType::Session, SessionDtor, sizeof(Session), 
            ProcTreeTag);
        SetNsObjTypeInfo(NsObjType::Job, JobDtor, sizeof(Job), ProcTreeTag);
        SetNsObjTypeInfo(NsObjType::Process, ProcessDtor, sizeof(Process), 
            ProcTreeTag);
        SetNsObjTypeInfo(NsObjType::Thread, ThreadDtor, sizeof(Thread), 
            ProcTreeTag);
    }
}
