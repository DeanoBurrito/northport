#pragma once

#include <core/Pmm.h>
#include <core/WiredHeap.h>
#include <RefCount.h>
#include <Flags.h>
#include <RangeAllocator.h>
#include <Error.h>

namespace Npk
{
    struct HatMap;
}

namespace Npk::Services
{
    enum class VmError
    {
        Success = 0,
        TryAgain,
        HatMapFailed,
        BadVmoOffset,
        InvalidArg,
    };

    enum class VmViewFlag
    {
        Write,
        Exec,
        Private,
    };

    enum class VmFaultType
    {
        Read,
        Write,
        Wire,
    };

    enum class VmPageFlag
    {
        IsOverlay = 0,
        Dirty = 1,
        Standby = 2,
    };

    using VmViewFlags = sl::Flags<VmViewFlag>;
    using VmPageFlags = sl::Flags<VmPageFlag, uint32_t>; //occupies PageInfo::vm::flags

    struct VmObject;
    struct VmDomain;
    class Vmm;

    constexpr size_t SwapKeyBackendBits = 4;
    constexpr size_t NoSwap = (1 << SwapKeyBackendBits) - 1;

    struct SwapKey
    {
        uint64_t backend : SwapKeyBackendBits;
        uint64_t index : 64 - SwapKeyBackendBits;
    };

    struct VmoRefCount //TODO: bit of a hack due to circular deps, can we fix this?
    {
        sl::RefCount count;
    };

    struct VmView
    {
        sl::ListHook vmmHook;
        sl::ListHook vmoHook;
        sl::Ref<VmoRefCount, &VmoRefCount::count> vmoRef;
        sl::FwdList<Core::PageInfo, &Core::PageInfo::vmObjList> overlay;
        Vmm* vmm;

        SwapKey key;
        uintptr_t base;
        size_t length;
        uint16_t offset;
        sl::SpinLock lock;
        VmViewFlags flags;
    };

    struct VmObject
    {
        VmoRefCount refCount;

        sl::SpinLock lock;
        bool isMmio;
        sl::FwdList<Core::PageInfo, &Core::PageInfo::vmObjList> content;
        sl::List<VmView, &VmView::vmoHook> views;
        size_t length;
    };

    struct npk_mdl //TODO: move to driver api headers
    {
        size_t length;
        size_t offset;
        uintptr_t* entries;
        size_t entry_count;
    };

    using VmmRangeAllocator = sl::RangeAllocator<uintptr_t, size_t, Core::WiredHeapAllocator>;

    class Vmm
    {
    private:
        VmDomain* domain;

        sl::SpinLock hatLock;
        HatMap* hatMap;
        sl::RwLock viewsLock;
        sl::List<VmView, &VmView::vmmHook> views;

        sl::SpinLock freeSpaceLock;
        VmmRangeAllocator freeSpace;

        //expects exclusive access to list (vmo/overlay lock tobe held), adds +1 to page's wireCount if found
        Core::PageInfo* FindPageInList(sl::FwdList<Core::PageInfo, &Core::PageInfo::vmObjList>& list, size_t offsetPfn);
        //expects viewsLock to be held, attempts to find a view for a particular virtual address.
        VmView* FindView(uintptr_t vaddr);

        static bool UnmapViewsOfPage(Core::PageInfo* page);

    public:
        sl::FwdListHook queueHook;

        static void InitKernel(sl::Span<VmView> kernelImage);
        static void DaemonThreadEntry(void* domain);

        inline VmDomain& GetDomain()
        { return *domain; }

        void Activate();
        //notifies the VMM of an attempt to access a particular address. This can happen
        //because of MMU mechanisms (improper permissions), or be called by the kernel
        //directly: see usage of VmFaultType::Wire.
        //The `addr` arg is absolute (i.e. not relative to the base of the address space),
        //the `type` arg indicates whether the access is a read, write or 'wire'.
        //Read and write should be self explanatory, 'wire' accesses count as a write for most
        //purposes but bias the wireCount of the backing by +1 before returning, so it cant be
        //paged out until the caller has decremented the wireCount.
        sl::ErrorOr<void, VmError> HandleFault(uintptr_t addr, VmFaultType type);

        sl::Opt<void*> AddView(VmObject* obj, size_t length, size_t offset, VmViewFlags flags, bool wire);
        void RemoveView(void* base);
        sl::Opt<void*> SplitView(void* addr);
        VmError ChangeViewFlags(void* addr, VmViewFlags newFlags);

        bool WireRange(void* base, size_t length);
        void UnwireRange(void* base, size_t length);
        bool AcquireMdl(void* base, size_t length, npk_mdl* mdl);
        void ReleaseMdl(npk_mdl** mdl);
    };

    struct VmDomain
    {
        uintptr_t zeroPage;

        sl::SpinLock listsLock;
        sl::FwdList<Core::PageInfo, &Core::PageInfo::mmList> activeList {};
        sl::FwdList<Core::PageInfo, &Core::PageInfo::mmList> dirtyList {};
        sl::FwdList<Core::PageInfo, &Core::PageInfo::mmList> standbyList {};
    };

    Vmm& KernelVmm();

    SL_ALWAYS_INLINE
    sl::Opt<void*> VmAlloc(VmObject* obj, size_t length, size_t offset, VmViewFlags flags)
    { return KernelVmm().AddView(obj, length, offset, flags, false); }

    SL_ALWAYS_INLINE
    sl::Opt<void*> VmAllocWired(VmObject* obj, size_t length, size_t offset, VmViewFlags flags)
    { return KernelVmm().AddView(obj, length, offset, flags, true); }

    SL_ALWAYS_INLINE
    sl::Opt<void*> VmAllocAnon(size_t length, VmViewFlags flags)
    { return KernelVmm().AddView(nullptr, length, 0, flags, false); }

    SL_ALWAYS_INLINE
    void VmFree(void* base)
    { return KernelVmm().RemoveView(base); }

    SL_ALWAYS_INLINE
    bool VmWire(void* base, size_t length)
    { return KernelVmm().WireRange(base, length); }

    SL_ALWAYS_INLINE
    void VmUnwire(void* base, size_t length)
    { return KernelVmm().UnwireRange(base, length); }
}

using Npk::Services::VmViewFlag;
