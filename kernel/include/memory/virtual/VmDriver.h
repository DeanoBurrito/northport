#pragma once

#include <stdint.h>
#include <stddef.h>
#include <memory/Vmm.h>
#include <arch/Hat.h>
#include <Locks.h>

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
    
    struct EventResult
    {
        //TODO: handle for waiting on backing operation to complete
        bool goodFault;
    };

    struct QueryResult
    {
        size_t alignment = 0;
        size_t length = 0;
        size_t hatMode = 0;
        bool success = false;
    };

    struct AttachResult
    {
        void* token = nullptr;
        size_t offset = 0;
        bool success = false;
    };

    struct VmDriverContext
    {
        sl::TicketLock& lock;
        HatMap* map;
        const VmRange& range;
    };

    /* Each virtual memory allocation has a type associated with it, the type determines
     * which VmDriver is responsible for backing this memory. The current VmDrivers are:
     * - Kernel: this maps the kernel binary itself at startup and is also used to map MMIO.
     * - Anon: makes anonymous/general working memory available.
     * - Vfs: acts as a bridge between the VFS + file cache and the VM subsystem, allows for
     *   memory mapping files.
     *
     * VmDrivers are treated as plugins to the VMM. When the VMM needs to take an action that
     * requires knowledge of how VM is provided it calls into the associated VmDriver.
     */
    class VmDriver
    {
    friend VirtualMemoryManager;
    private:
        static VmDriver* GetDriver(VmFlags flags);
        static const char* GetName(VmFlags flags);

    public:
        static void InitAll();

        virtual void Init(uintptr_t enableFeatures) = 0;
        
        //Called when the VMM is notified of a page fault within an existing range that is backed by
        //this VmDriver. The driver informs the VMM (and rest of the kernel) what it should do with
        //the faulting code depending on the EventResult.
        virtual EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) = 0;

        //The VMM wants to modify an active VM range, the valid args indicate which properties of
        //the range are requested for modification.
        //Returns whether the operation was successful or not.
        virtual bool ModifyRange(VmDriverContext& context, sl::Opt<VmFlags> flags) = 0;
        
        //The VMM wants to know how much space (and with what alignment) a VM allocation would
        //require. This is purely informational, no memory mappings are modified here.
        virtual QueryResult Query(size_t length, VmFlags flags, uintptr_t attachArg) = 0;
        
        //The VMM has allocated space (in a way that satisfies a previously call to `Query()`).
        //This function is responsible for backing the virtual memory with something useful.
        //Note that lazy techniques like CoW and demand paging are used which may result in the
        //actual mapping happening partially in this function, and partially in response to 
        //a page fault.
        virtual AttachResult Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg) = 0;

        //The VMM is freeing a range of virtual memory and wants to remove the backing memory
        //that was previously attached there. The VmDriver should also clean up any auxiliary
        //resources here (used for communicating between Attach() and HandleFault() calls).
        virtual bool Detach(VmDriverContext& context) = 0;
    };
}
