#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>
#include <Locks.h>
#include <memory/Vmm.h>
#include <arch/Paging.h>

namespace Npk::Memory
{ class VirtualMemoryManager; }

namespace Npk::Memory::Virtual
{
    constexpr PageFlags ConvertFlags(VmFlags flags)
    {
        /*  VMFlags are stable across platforms, while PageFlags have different meanings
            depending on the ISA. This provides source-level translation between the two. */
        PageFlags value = PageFlags::None;
        if (flags & VmFlags::Write)
            value |= PageFlags::Write;
        if (flags & VmFlags::Execute)
            value |= PageFlags::Execute;
        if (flags & VmFlags::User)
            value |= PageFlags::User;
        
        return value;
    }

    enum class EventResult
    {
        Continue,   //thread is allowed to continue.
        Kill,       //thread did something bad, kill it.
        Suspend,    //op will need time to complete, suspend thread or return async-handle.
    };

    enum class EventType
    {
        PageFault,
    };

    enum class VmDriverType : size_t
    {
        //0 is reserved, and will result in nullptr being returned.
        //This forces alloc requests to set the `type` field.
        Anon = 1,
        Kernel = 2,
        
        EnumCount
    };

    constexpr inline const char* VmDriverTypeStrs[] = 
    {
        "null", "kernel/mmio", "anonymous"
    };

    struct VmDriverContext
    {
        void* ptRoot;
        sl::TicketLock& lock;
        uintptr_t vaddr;
        size_t length;
        uintptr_t token;
        VmFlags flags;
    };
    
    class VmDriver
    {
    friend VirtualMemoryManager;
    private:
        static VmDriver* GetDriver(VmDriverType name);

    public:
        static void InitEarly();

        virtual void Init() = 0;
        virtual VmDriverType Type() = 0;
        
        //driver needs to handle an event.
        virtual EventResult HandleEvent(VmDriverContext& context, EventType type, uintptr_t addr, uintptr_t eventArg) = 0;
        //driver should attach itself to a range, and prepare to back it accordingly.
        virtual sl::Opt<size_t> AttachRange(VmDriverContext& context, uintptr_t attachArg) = 0;
        //driver should release resources backing a range.
        virtual bool DetachRange(VmDriverContext& context) = 0;
    };
}
