#include <services/Vmm.h>
#include <services/VmPagers.h>
#include <arch/Hat.h>
#include <core/Log.h>
#include <Entry.h>
#include <Hhdm.h>
#include <UnitConverter.h>
#include <Memory.h>

namespace Npk::Services
{
    static_assert(offsetof(VmObject, refCount) == 0);

    static uintptr_t zeroPage;
    static sl::SpinLock vmListsLock {};
    static sl::FwdList<Core::PageInfo, &Core::PageInfo::mmList> activeList {};
    static sl::FwdList<Core::PageInfo, &Core::PageInfo::mmList> standbyList {};

    constexpr HatFlags MakeHatFlags(VmViewFlags flags, bool user)
    {
        HatFlags ret {};
        if (flags.Has(VmViewFlag::Write))
            ret |= HatFlag::Write;
        if (flags.Has(VmViewFlag::Exec))
            ret |= HatFlag::Execute;
        ret |= user ? HatFlag::User : HatFlag::Global;

        return ret;
    }

    sl::ErrorOr<void, VmError> Vmm::WirePage(VmView& view, size_t offset)
    {
        Core::PageInfo* info = FindPageOfView(view, offset);

        if (!view.vmoRef.Valid())
        {
            //there's no backing vm object, so its an anon (private) mapping
            if (info == Core::PmLookup(zeroPage))
            {
                //view[offset] is mapped to the zero page, with a readonly mapping.
                //we'll unmap it and set `info` to null so it gets allocated a fresh page
                //later on.
                uintptr_t paddr;
                size_t mode;
                HatDoUnmap(hatMap, view.base + offset, paddr, mode, true);
                info = nullptr;
            }

            if (info == nullptr)
            {
                //view[offset] was either mapped to the zero page or not mapped at all,
                //we'll map a fresh page, zeroing it if needed.
                const auto maybePage = Core::PmAlloc();
                if (!maybePage.HasValue())
                    return VmError::TryAgain;

                info = Core::PmLookup(*maybePage);
                if (!info->pm.zeroed)
                {
                    sl::memset(reinterpret_cast<void*>(AddHhdm(*maybePage)), 0, PageSize());
                    info->pm.zeroed = true;
                }

                const HatFlags hatFlags = MakeHatFlags(view.flags, hatMap != KernelMap());
                if (HatDoMap(hatMap, view.base + offset, *maybePage, 0, hatFlags, false) 
                    != HatError::Success)
                {
                    Core::PmFree(*maybePage);
                    return VmError::HatMapFailed;
                }

                info->vm.offset = offset >> PfnShift();
                info->vm.wireCount = 1;
                info->vm.flags = VmPageFlags(VmPageFlag::IsOverlay).Raw();
                info->vm.vmo = &view;
                view.overlay.InsertSorted(info, 
                    [](auto* a, auto* b) -> bool { return a->vm.offset < b->vm.offset; });

                vmListsLock.Lock();
                activeList.PushBack(info);
                vmListsLock.Unlock();
            }
            //else: dedicated page is already mapped, and will have proper permissions

            return sl::NoError;

        }

        VmObject* vmo = reinterpret_cast<VmObject*>(&*view.vmoRef);
        if (vmo->isMmio)
        {
            //mmio objets are always mapped with their full permissions, so if we're here
            //it's because the view is not mapped.
            auto maybePaddr = GetMmioVmoPage(vmo, offset);
            if (!maybePaddr.HasValue())
                return VmError::BadVmoOffset;

            HatFlags hatFlags = MakeHatFlags(view.flags, false);
            hatFlags |= GetMmioVmoHatFlags(vmo, offset);
            if (HatDoMap(hatMap, view.base + offset, *maybePaddr, 0, hatFlags, false) 
                != HatError::Success)
            {
                return VmError::HatMapFailed;
            }
            return sl::NoError;
        }

        //TODO: implement file mappings
        ASSERT_UNREACHABLE();
    }

    void Vmm::UnwirePage(VmView& view, size_t offset)
    {
        offset >>= PfnShift();
        ASSERT_UNREACHABLE();
    }

    Core::PageInfo* Vmm::FindPageOfView(VmView& view, size_t offset)
    {
        offset >>= PfnShift();

        //check view's overlay for a page at this particular offset
        for (auto it = view.overlay.Begin(); it != view.overlay.End(); ++it)
        {
            if (offset < it->vm.offset)
                break;
            if (offset != it->vm.offset)
                continue;

            it->vm.wireCount++;
            return &*it;
        }

        //nothing in the overlay, check the backing obj itself
        if (!view.vmoRef.Valid())
            return nullptr;

        VmObject& vmo = *reinterpret_cast<VmObject*>(&*view.vmoRef);
        sl::ScopedLock vmoLock(vmo.lock);

        //trying to access beyong the end of the backing object
        if (offset >= vmo.length)
            return nullptr;

        for (auto it = vmo.content.Begin(); it != vmo.content.End(); ++it)
        {
            if (offset < it->vm.offset)
                break;
            if (offset != it->vm.offset)
                continue;

            it->vm.wireCount++;
            return &*it;
        }

        return nullptr;
    }

    VmView* Vmm::FindView(uintptr_t vaddr)
    {
        for (auto it = views.Begin(); it != views.End(); ++it)
        {
            if (vaddr < it->base)
                return nullptr;
            if (vaddr >= it->base && vaddr < it->base + it->length)
                return &*it;
        }

        return nullptr;
    }

    void Vmm::InitKernel(sl::Span<VmView> kernelImage)
    {
        //Note that this is setup for the VMM itself, we dont interact with the HAT layer here
        //as each core switches to the kernel-managed PTs in `Entry.cpp:PerCoreEntry()`

        Vmm& kvmm = KernelVmm();
        kvmm.hatMap = KernelMap();

        const auto zeroPageAlloc = Core::PmAlloc();
        ASSERT_(zeroPageAlloc.HasValue());
        zeroPage = *zeroPageAlloc;
        sl::memset(reinterpret_cast<void*>(zeroPage + hhdmBase), 0, PageSize());

        const uintptr_t lowerBound = EarlyVmControl(false);
        const uintptr_t upperBound = -PageSize();

        kvmm.freeSpaceLock.Lock();
        new(&kvmm.freeSpace) VmmRangeAllocator(lowerBound, upperBound, PfnShift());

        for (size_t i = 0; i < kernelImage.Size(); i++)
            kvmm.freeSpace.Claim(kernelImage[i].base, kernelImage[i].length);
        kvmm.freeSpaceLock.Unlock();

        const size_t totalAddrSpace = upperBound - lowerBound;
        const auto conv = sl::ConvertUnits(totalAddrSpace);
        Log("Kernel VMM initialized: 0x%tx -> 0x%tx (0x%zx, %zu.%zu %sB)", LogLevel::Info, 
            lowerBound, upperBound, totalAddrSpace, conv.major, conv.minor, conv.prefix);
        
        const auto earlyConv = sl::ConvertUnits(lowerBound - (hhdmBase + hhdmLength));
        Log("EarlyVm used %zu.%zu %sB (up to 0x%tx)", LogLevel::Verbose, earlyConv.major,
            earlyConv.minor, earlyConv.prefix, lowerBound);
    };

    bool Vmm::HandlePageFault(uintptr_t addr, VmFaultFlags flags, size_t lengthHint)
    {
        ASSERT_UNREACHABLE(); (void)addr; (void)flags; (void)lengthHint;
    }

    sl::Opt<void*> Vmm::AddView(VmObject* obj, size_t length, size_t offset, VmViewFlags flags, bool wire)
    {
        const size_t offsetBias = offset & PageMask();

        freeSpaceLock.Lock();
        const auto maybeBase = freeSpace.Alloc(offsetBias + length);
        freeSpaceLock.Unlock();

        if (!maybeBase.HasValue())
            return {};

        VmView* view = NewWired<VmView>();
        if (view == nullptr)
        {
            freeSpaceLock.Lock();
            freeSpace.Free(*maybeBase, offsetBias + length);
            freeSpaceLock.Unlock();

            DeleteWired(view);
            return {};
        }

        view->base = *maybeBase + offsetBias;
        view->length = length;
        view->offset = offset;
        view->flags = flags;
        view->vmoRef = nullptr;

        if (obj != nullptr)
        {
            view->vmoRef = &obj->refCount;

            sl::ScopedLock scopeLock(obj->lock);
            obj->views.PushBack(view);
        }

        viewsLock.WriterLock();
        views.InsertSorted(view, [](VmView* a, VmView* b) -> bool
            { return a->length < b->length; });
        viewsLock.WriterUnlock();

        if (wire && !Wire(reinterpret_cast<void*>(view->base), view->length))
        {
            //TODO: remove view
            return {};
        }

        return reinterpret_cast<void*>(view->base);
    }
    
    void Vmm::RemoveView(void* base)
    {
        ASSERT_UNREACHABLE(); (void)base;
    }

    bool Vmm::Wire(void* base, size_t length)
    {
        const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(base);

        bool failure = false;
        size_t undoCount = 0;
        viewsLock.ReaderLock();
        for (size_t i = 0; i < length; i += PageSize())
        {
            VmView* view = FindView(baseAddr + i);
            if (view == nullptr)
                continue;

            const uintptr_t offset = baseAddr + i - view->base;
            if (WirePage(*view, offset).HasError())
            {
                undoCount = i;
                failure = true;
                break;
            }
        }

        //Wiring memory should be an atomic operation, if we hit an error earlier
        //partway through the process we need to undo the PageIn() ops already done.
        for (size_t i = 0; i < undoCount; i += PageSize())
        {
            VmView* view = FindView(baseAddr + i);
            if (view == nullptr)
                continue;

            const uintptr_t offset = baseAddr + i - view->base;
            UnwirePage(*view, offset);
        }
        viewsLock.ReaderUnlock();

        return !failure;
    }

    void Vmm::Unwire(void* base, size_t length)
    {
        ASSERT_UNREACHABLE(); (void)base; (void)length;
    }

    Vmm kernelVmm;
    Vmm& KernelVmm()
    {
        return kernelVmm;
    }
}
