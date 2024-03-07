#include <tasking/Threads.h>
#include <tasking/Scheduler.h>
#include <debug/Log.h>

namespace Npk::Tasking
{
    constexpr size_t DefaultThreadStackSize = 64 * KiB;

    static sl::Span<uint8_t> GetAttribInternal(sl::Span<ProgramAttribHeader> headers, ProgramAttribType type)
    {
        for (size_t i = 0; i < headers.Size(); i++)
        {
            if (headers[i].type == type)
                return { static_cast<uint8_t*>(headers[i].data), headers[i].length };
        }

        return {};
    }

    static void SetAttribInternal(sl::Span<ProgramAttribHeader> headers, ProgramAttribType type, sl::Span<uint8_t> data)
    {
        /* Strategy:
         * - figure out size of new buffer, realloc if necessary
         */
        ASSERT_UNREACHABLE();
    }

    Process kernelProcess;
    Process& Process::Kernel()
    { return kernelProcess; }

    Process& Process::Current()
    { return Thread::Current().Parent(); }

    Process* Process::Create()
    { 
        auto maybeId = ProgramManager::Global().CreateProcess();
        if (!maybeId.HasValue())
            return nullptr;
        return ProgramManager::Global().GetProcess(*maybeId);
    }

    sl::Span<uint8_t> Process::GetAttrib(ProgramAttribType type)
    { 
        attribsLock.ReaderLock();
        auto found = GetAttribInternal(attribs, type);
        attribsLock.ReaderUnlock();
        return found;
    }

    void Process::SetAttrib(ProgramAttribType type, sl::Span<uint8_t> data)
    { 
        attribsLock.WriterLock();
        SetAttribInternal(attribs, type, data);
        attribsLock.WriterUnlock();
    }

    sl::StringSpan Process::Name(sl::StringSpan newName)
    {
        auto existing = GetAttribInternal(attribs, ProgramAttribType::Name);
        if (!newName.Empty())
        {
            sl::Span<uint8_t> data((uint8_t*)newName.Begin(), newName.SizeBytes());
            SetAttribInternal(attribs, ProgramAttribType::Name, data);
        }

        return sl::StringSpan(reinterpret_cast<const char*>(existing.Begin()), existing.SizeBytes() / sizeof(char));
    }

    Thread& Thread::Current()
    { 
        ASSERT_(CoreLocalAvailable());
        ASSERT_(CoreLocal()[LocalPtr::Thread] != nullptr);

        return *static_cast<Thread*>(CoreLocal()[LocalPtr::Thread]);
    }

    Thread* Thread::Create(size_t procId, ThreadEntry entry, void* arg)
    {
        auto maybeId = ProgramManager::Global().CreateThread(procId, entry, arg, NoAffinity, DefaultThreadStackSize);
        if (!maybeId.HasValue())
            return nullptr;
        return ProgramManager::Global().GetThread(*maybeId);
    }

    Thread* Thread::Get(size_t id)
    { return ProgramManager::Global().GetThread(id); }

    sl::Span<uint8_t> Thread::GetAttrib(ProgramAttribType type)
    { 
        attribsLock.ReaderLock();
        auto found = GetAttribInternal(attribs, type);
        attribsLock.ReaderUnlock();
        return found;
    }

    void Thread::SetAttrib(ProgramAttribType type, sl::Span<uint8_t> data)
    { 
        attribsLock.WriterLock();
        SetAttribInternal(attribs, type, data);
        attribsLock.WriterUnlock();
    }

    sl::StringSpan Thread::Name(sl::StringSpan newName)
    {
        auto existing = GetAttribInternal(attribs, ProgramAttribType::Name);
        if (!newName.Empty())
        {
            sl::Span<uint8_t> data((uint8_t*)newName.Begin(), newName.SizeBytes());
            SetAttribInternal(attribs, ProgramAttribType::Name, data);
        }

        return sl::StringSpan(reinterpret_cast<const char*>(existing.Begin()), existing.SizeBytes() / sizeof(char));
    }

    void Thread::SetAffinity(size_t newAffinity)
    {
        ASSERT_UNREACHABLE();
    }

    void Thread::Start(void* arg)
    {
        VALIDATE_(state == ThreadState::Setup, );
        state = ThreadState::Ready;

        Scheduler::Global().EnqueueThread(this);
    }

    void Thread::Exit(size_t code)
    {
        ASSERT_UNREACHABLE();
    }

    ProgramManager globalProgramManager;
    ProgramManager& ProgramManager::Global()
    { return globalProgramManager; }

    void ProgramManager::Init()
    {
        nextId = 1;
        kernelProcess.id = nextId++;
        kernelProcess.attribs = { nullptr, 0 };
        processes.PushBack(&kernelProcess);

        Log("Program manager initialized.", LogLevel::Info);
    }

    void ProgramManager::SaveCurrentFrame(TrapFrame* frame)
    {
        Thread::Current().frame = frame;
    }

    TrapFrame* ProgramManager::GetNextFrame()
    {
        return Thread::Current().frame;
    }

    sl::Opt<size_t> ProgramManager::CreateProcess()
    {
        Process* proc = new Process();
        proc->id = nextId++;
        proc->attribs = { nullptr, 0 };

        procLock.WriterLock();
        processes.PushBack(proc);
        procLock.WriterUnlock();

        Log("Created process %lu.", LogLevel::Verbose, proc->id);
        return proc->id;
    }

    sl::Opt<size_t> ProgramManager::CreateThread(size_t procId, ThreadEntry entry, void* arg, size_t affinity, size_t stackSize)
    {
        auto parent = GetProcess(procId);
        VALIDATE_(parent != nullptr, {});
        const VmFlags execFlag = VmFlag::Execute;
        const uintptr_t entryAddr = reinterpret_cast<uintptr_t>(entry);
        VALIDATE_(parent->vmm.MemoryExists(entryAddr, 2, execFlag), {});

        //TODO: for user stacks we dont need to immediately back the memory (the const 1 arg)
        auto maybeStack = parent->vmm.Alloc(stackSize, 1, VmFlag::Anon | VmFlag::Write); //TODO: guarded?
        VALIDATE_(maybeStack, {});

        Thread* thread = new Thread();
        thread->id = nextId++;
        thread->attribs = { nullptr, 0 };
        thread->affinity = affinity;
        thread->engineId= NoAffinity;
        thread->state = ThreadState::Setup;
        thread->extRegs = nullptr; //these are populated on-demand

        TrapFrame setupFrame {};
        InitTrapFrame(&setupFrame, *maybeStack + stackSize, entryAddr, false);
        SetTrapFrameArg(&setupFrame, 0, arg);
        const uintptr_t frameAddr = (*maybeStack + stackSize) - sizeof(TrapFrame);
        ASSERT_(parent->vmm.CopyIn((void*)frameAddr, &setupFrame, sizeof(TrapFrame)) == sizeof(TrapFrame));
        thread->frame = reinterpret_cast<TrapFrame*>(frameAddr);
        
        threadLock.WriterLock();
        threads.PushBack(thread);
        threadLock.WriterUnlock();

        Log("Created thread %lu.%lu, entry=%p", LogLevel::Verbose, procId, thread->id, entry);
        return thread->id;
    }

    Thread* ProgramManager::GetThread(size_t id)
    {
        threadLock.ReaderLock();
        for (size_t i = 0; i < threads.Size(); i++)
        {
            if (threads[i] == nullptr || threads[i]->id != id)
                continue;

            Thread* found = threads[i];
            threadLock.ReaderUnlock();
            return found;
        }
        threadLock.ReaderUnlock();
        return nullptr;
    }

    Process* ProgramManager::GetProcess(size_t id)
    {
        procLock.ReaderLock();
        for (size_t i = 0; i < processes.Size(); i++)
        {
            if (processes[i] == nullptr || processes[i]->id != id)
                continue;

            Process* found = processes[i];
            procLock.ReaderUnlock();
            return found;
        }
        procLock.ReaderUnlock();
        return nullptr;
    }
}
