#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/Vmm.h>
#include <arch/Hat.h>
#include <Locks.h>

namespace Npk::Memory
{ class VirtualMemoryManager; }

namespace Npk::Memory::Virtual
{
    constexpr HatFlags ConvertFlags(VmFlags flags)
    {
        /*  VMFlags are stable across platforms, while HatFlags have different meanings
            depending on the ISA. This provides source-level translation between the two. */
        HatFlags value = HatFlags::None;
        if (flags.Has(VmFlag::Write))
            value |= HatFlags::Write;
        if (flags.Has(VmFlag::Execute))
            value |= HatFlags::Execute;
        if (flags.Has(VmFlag::User))
            value |= HatFlags::User;

        return value;
    }

    enum class EventResult
    {
        Continue,   //thread is allowed to continue.
        Kill,       //thread did something bad, kill it.
        Suspend,    //op will need time to complete, suspend thread or return async-handle.
    };

    struct AttachResult
    {
        bool success;
        size_t token;
        uintptr_t baseOffset;
        size_t deadLength;
    };

    struct VmDriverContext
    {
        sl::TicketLock& lock;
        HatMap* map;
        VmRange range;
    };

    class VmDriver
    {
    friend VirtualMemoryManager;
    private:
        static VmDriver* GetDriver(VmFlags flags);

    public:
        static void InitAll();

        virtual void Init(uintptr_t enableFeatures) = 0;
        
        virtual EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) = 0;
        virtual AttachResult Attach(VmDriverContext& context, uintptr_t attachArg) = 0;
        virtual bool Detach(VmDriverContext& context) = 0;
    };
}
