#include <tasking/Threads.h>
#include <tasking/Scheduler.h>
#include <tasking/Waitable.h>
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

    static void SetAttribInternal(sl::Span<ProgramAttribHeader>& headers, ProgramAttribType type, sl::Span<uint8_t> data)
    {
        auto existingBuffer = GetAttribInternal(headers, ProgramAttribType::AttribBuffer);
        auto existingAttrib = GetAttribInternal(headers, type);

        size_t newSize = existingBuffer.Size();
        if (existingAttrib.Empty())
            newSize += data.Size() + sizeof(ProgramAttribHeader); //TODO: what if attrib exists but is changing size?
        else
            newSize = (newSize - existingAttrib.Size()) + data.Size();
        if (existingBuffer.Empty())
            newSize += sizeof(ProgramAttribHeader);

        if (newSize != existingBuffer.Size())
        {
            void* newBuffer = malloc(newSize);
            ASSERT_(newBuffer != nullptr);
            sl::NativePtr dataBuffer = sl::NativePtr(newBuffer).Offset(headers.SizeBytes());

            ProgramAttribHeader* destHeaders = static_cast<ProgramAttribHeader*>(newBuffer);
            size_t destHeadersCount = headers.Size();
            for (size_t i = 0; i < headers.Size(); i++)
            {
                destHeaders[i] = headers[i];
                if (destHeaders[i].type == ProgramAttribType::AttribBuffer)
                {
                    destHeaders[i].data = newBuffer;
                    destHeaders[i].length = newSize;
                }
            }
            if (existingAttrib.Empty())
            {
                auto& newHeader = destHeaders[destHeadersCount++];
                newHeader.type = type;
                newHeader.length = data.Size();
                dataBuffer = dataBuffer.Offset(sizeof(ProgramAttribHeader));
            }
            if (existingBuffer.Empty())
            {
                auto& newHeader = destHeaders[destHeadersCount++];
                newHeader.type = ProgramAttribType::AttribBuffer;
                newHeader.data = newBuffer;
                newHeader.length = newSize;
            }

            for (size_t i = 0; i < headers.Size(); i++)
            {
                if (headers[i].length == 0 || headers[i].type == ProgramAttribType::AttribBuffer)
                    continue;
                ASSERT_UNREACHABLE(); //TODO: finish this
            }

            headers = sl::Span<ProgramAttribHeader>(destHeaders, destHeadersCount);
        }

        for (size_t i = 0; i < headers.Size(); i++)
        {
            if (headers[i].type != type)
                continue;

            headers[i].data = data.Begin();
            headers[i].length = data.Size();
            break;
        }
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
        auto maybeId = ProgramManager::Global().CreateThread(procId, entry, arg, 
            NoCoreAffinity, DefaultThreadStackSize);

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
        
        if (arg != nullptr)
        {
            TrapFrame localFrame {};
            ASSERT_(parent->Vmm().CopyOut(&localFrame, frame, sizeof(TrapFrame)) == sizeof(TrapFrame));
            SetTrapFrameArg(&localFrame, 0, arg);
            ASSERT_(parent->Vmm().CopyIn(frame, &localFrame, sizeof(TrapFrame)) == sizeof(TrapFrame));
        }

        Scheduler::Global().EnqueueThread(this);
    }

    void Thread::Exit(size_t code)
    {
        //TODO: put thread into reclaimable queue
        Log("Thread %lu.%lu exiting with code %lu", LogLevel::Verbose, parent->Id(), id, code);
        const bool selfExit = CoreLocal()[LocalPtr::Thread] == this;

        const RunLevel prevLevel = RaiseRunLevel(RunLevel::Dpc);
        Scheduler::Global().DequeueThread(this);

        schedLock.Lock();
        state = ThreadState::Dead;
        schedLock.Unlock();
        LowerRunLevel(prevLevel);

        if (selfExit)
        {
            Scheduler::Global().Yield(true);
            ASSERT_UNREACHABLE();
        }
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

    TrapFrame** ProgramManager::GetCurrentFrameStore()
    {
        return &Thread::Current().frame;
    }

    bool ProgramManager::ServeException(ProgramException exception)
    {
        return false; //TODO: routing for this
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

        //TODO: for user stacks we dont need to immediately back the memory (the const 3 arg)
        auto maybeStack = parent->vmm.Alloc(stackSize, 3, VmFlag::Anon | VmFlag::Write); //TODO: guarded?
        VALIDATE_(maybeStack, {});

        Thread* thread = new Thread();
        thread->parent = parent;
        thread->id = nextId++;
        thread->attribs = { nullptr, 0 };
        thread->affinity = affinity;
        thread->state = ThreadState::Setup;
        thread->extRegs = nullptr; //these are populated on-demand

        TrapFrame setupFrame {};
        InitTrapFrame(&setupFrame, *maybeStack + stackSize, entryAddr, false);
        SetTrapFrameArg(&setupFrame, 0, arg);
        const uintptr_t frameAddr = sl::AlignDown((*maybeStack + stackSize) - sizeof(TrapFrame), alignof(TrapFrame));
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
