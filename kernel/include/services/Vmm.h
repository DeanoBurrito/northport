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
    };

    enum class VmViewFlag
    {
        Write,
        Exec,
        Private,
    };

    enum class VmFaultFlag
    {
        Read,
        Write,
        Fetch,
        User,
    };

    enum class VmPageFlag
    {
        Locked = 0,
        IsOverlay = 1,
    };

    using VmViewFlags = sl::Flags<VmViewFlag>;
    using VmFaultFlags = sl::Flags<VmFaultFlag>;
    using VmPageFlags = sl::Flags<VmPageFlag, uint32_t>; //occupies PageInfo::vm::flags

    struct VmObject;

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

        size_t offset;
        uintptr_t base;
        size_t length;
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

    using VmmRangeAllocator = sl::RangeAllocator<uintptr_t, size_t, Core::WiredHeapAllocator>;

    class Vmm
    {
    private:
        sl::SpinLock hatLock;
        HatMap* hatMap;
        sl::RwLock viewsLock;
        sl::List<VmView, &VmView::vmmHook> views;

        sl::SpinLock freeSpaceLock;
        VmmRangeAllocator freeSpace;

        //expects viewsLock to be held
        sl::ErrorOr<void, VmError> WirePage(VmView& view, size_t offset);
        //expects viewsLock to be held
        void UnwirePage(VmView& view, size_t offset);
        //expects viewLock to be held, if it finds a page (either in the overlay or the backing obj),
        //the page's wireCount is incremented before returning. The caller is reponsible for decrementing
        //it when finished. TODO: could we automate the refcount with sl::Ref<T>?
        Core::PageInfo* FindPageOfView(VmView& view, size_t offset);
        //expects viewsLock to be held, attempts to find a view for a particular virtual address.
        VmView* FindView(uintptr_t vaddr);

    public:
        static void InitKernel(sl::Span<VmView> kernelImage);

        bool HandlePageFault(uintptr_t addr, VmFaultFlags flags);

        sl::Opt<void*> AddView(VmObject* obj, size_t length, size_t offset, VmViewFlags flags, bool wire);
        void RemoveView(void* base);
        bool Wire(void* base, size_t length);
        void Unwire(void* base, size_t length);
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
    { return KernelVmm().Wire(base, length); }

    SL_ALWAYS_INLINE
    void VmUnwire(void* base, size_t length)
    { return KernelVmm().Unwire(base, length); }
}

using Npk::Services::VmViewFlag;
