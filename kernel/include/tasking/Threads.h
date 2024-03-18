#pragma once

#include <arch/Platform.h>
#include <memory/Vmm.h>
#include <containers/Vector.h>
#include <containers/List.h>
#include <Locks.h>
#include <Handle.h>
#include <Atomic.h>

namespace Npk::Tasking
{
    enum class ProgramAttribType : unsigned
    {
        AttribBuffer, //describes the attrib buffer itself
        Name,
        DriverShadow,
    };

    struct ProgramAttribHeader
    {
        ProgramAttribType type;
        unsigned length;
        void* data;
    };

    class ProgramManager;
    class Scheduler;
    struct WorkQueue;

    class Process
    {
    friend ProgramManager;
    friend VMM;
    private:
        VMM vmm;
        size_t id;

        sl::RwLock attribsLock;
        sl::Span<ProgramAttribHeader> attribs;

    public:
        static Process& Kernel();
        static Process& Current();
        static Process* Create();

        [[gnu::always_inline]]
        inline size_t Id() const
        { return id; }

        sl::Span<uint8_t> GetAttrib(ProgramAttribType type);
        void SetAttrib(ProgramAttribType type, sl::Span<uint8_t> data);
        inline void ClearAttrib(ProgramAttribType type)
        { SetAttrib(type, {}); }

        sl::StringSpan Name(sl::StringSpan name = {});
    };

    enum class ThreadState
    {
        Setup = 0,
        Dead = 1,
        Ready = 2,
        Queued = 3,
        Running = 4,
    };

    constexpr size_t NoAffinity = -1ul;

    using ThreadEntry = void (*)(void*);

    class Thread
    {
    friend ProgramManager;
    friend Scheduler;
    friend sl::IntrFwdList<Thread>;
    private:
        TrapFrame* frame;
        ExtendedRegs* extRegs;

        sl::SpinLock schedLock;
        Thread* next;
        ThreadState state;
        Process* parent;
        size_t id;
        size_t affinity;
        union {
            WorkQueue* queue;
            size_t engineId;
        } engineOrQueue;

        sl::RwLock attribsLock;
        sl::Span<ProgramAttribHeader> attribs;

        Thread() = default;

    public:
        static Thread& Current();
        static Thread* Create(size_t procId, ThreadEntry entry, void* arg);
        static Thread* Get(size_t id);

        [[gnu::always_inline]]
        inline ThreadState State() const
        { return state; }

        [[gnu::always_inline]]
        inline size_t Id() const
        { return id; }

        [[gnu::always_inline]]
        inline Process& Parent() const
        { return *parent; }

        [[gnu::always_inline]]
        inline size_t EngineId() const
        { return state == ThreadState::Running ? engineOrQueue.engineId : NoAffinity; }

        [[gnu::always_inline]]
        inline size_t GetAffinity() const
        { return affinity; }

        sl::Span<uint8_t> GetAttrib(ProgramAttribType type);
        void SetAttrib(ProgramAttribType type, sl::Span<uint8_t> data);
        inline void ClearAttrib(ProgramAttribType type)
        { SetAttrib(type, {}); }

        sl::StringSpan Name(sl::StringSpan name = {});

        void SetAffinity(size_t newAffinity);
        void Start(void* arg);
        void Exit(size_t code);
        //TODO: Sleep() and Join()
    };

    class ProgramManager
    {
    private:
        sl::RwLock procLock;
        sl::RwLock threadLock;
        sl::Vector<Process*> processes; //TODO: hashmap/xmap of id -> pointers
        sl::Vector<Thread*> threads;
        sl::Atomic<size_t> nextId; //TODO: proper ID allocator with 'dead/pending' state for recycling

    public:
        static ProgramManager& Global();

        void Init();

        void SaveCurrentFrame(TrapFrame* frame);
        TrapFrame* GetNextFrame();

        sl::Opt<size_t> CreateProcess();
        sl::Opt<size_t> CreateThread(size_t procId, ThreadEntry entry, void* arg, size_t affinity, size_t stackSize);
        Thread* GetThread(size_t id);
        Process* GetProcess(size_t id);
        //TODO: destroy process/thread
    };
}
