#include <services/Vmm.h>
#include <services/VmPagers.h>
#include <arch/Hat.h>
#include <core/Log.h>
#include <core/Smp.h>
#include <core/Config.h>
#include <Entry.h>
#include <Hhdm.h>
#include <UnitConverter.h>
#include <KernelThread.h>
#include <Memory.h>

namespace Npk::Services
{
    constexpr size_t DefaultDaemonWakeMs = 500;

    static_assert(offsetof(VmObject, refCount) == 0);

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
            if (info == Core::PmLookup(domain->zeroPage))
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

                domain->listsLock.Lock();
                domain->activeList.PushBack(info);
                domain->listsLock.Unlock();
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

    bool Vmm::UnmapViewsOfPage(Core::PageInfo* page)
    {
        ASSERT_(page != nullptr);

        if (VmPageFlags(page->vm.flags).Has(VmPageFlag::IsOverlay))
        {
            VmView& view = *static_cast<VmView*>(page->vm.vmo);
            const uintptr_t vaddr = view.base + (page->vm.offset << PfnShift());
            
            view.vmm->hatLock.Lock();
            uintptr_t ignored0;
            size_t ignored1;
            HatDoUnmap(view.vmm->hatMap, vaddr, ignored0, ignored1, true);
            view.vmm->hatLock.Unlock();
        }
        else
            ASSERT_UNREACHABLE(); //TODO: Vmo handling

        return true;
    }

    VmDomain defaultVmDomain {};

    void Vmm::InitKernel(sl::Span<VmView> kernelImage)
    {
        //Note that this is setup for the VMM itself, we dont interact with the HAT layer here
        //as each core switches to the kernel-managed PTs in `Entry.cpp:PerCoreEntry()`

        Vmm& kvmm = KernelVmm();
        kvmm.hatMap = KernelMap();
        kvmm.domain = &defaultVmDomain;

        const auto zeroPageAlloc = Core::PmAlloc();
        ASSERT_(zeroPageAlloc.HasValue());
        kvmm.domain->zeroPage = *zeroPageAlloc;
        sl::memset(reinterpret_cast<void*>(kvmm.domain->zeroPage + hhdmBase), 0, PageSize());

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

    void Vmm::DaemonThreadEntry(void* domainArg)
    {
        using namespace Core;
        VmDomain& dom = *static_cast<VmDomain*>(domainArg);

        const size_t wakeMs = GetConfigNumber("kernel.vmd.wake_timeout_ms", DefaultDaemonWakeMs);
        WaitEntry waitEntry;
        Waitable wakeEvent;
        wakeEvent.Reset(0, 1);
        Pmm::Global().AttachVmDaemon(wakeEvent);

        while (true)
        {
            const auto result = WaitOne(&wakeEvent, &waitEntry, sl::TimeCount(sl::Millis, wakeMs));
            if (result == WaitResult::Cancelled)
                break;

            //first stage: update sampling data of pages, determine what can safely be moved to standby/dirty lists
            dom.listsLock.Lock();

            auto prev = dom.activeList.End();
            auto scan = dom.activeList.Begin();
            while (scan != dom.activeList.End())
            {
                PageInfo* page = &*scan;
                const bool evict = true; //TODO: logic here (aging?)
                if (!evict)
                {
                    prev = scan;
                    ++scan;
                    continue;
                }

                if (!UnmapViewsOfPage(page))
                {
                    prev = scan;
                    ++scan;
                    continue;
                }

                if (prev == dom.activeList.End())
                {
                    dom.activeList.PopFront();
                    scan = dom.activeList.Begin();
                }
                else
                {
                    dom.activeList.EraseAfter(prev);
                    prev = scan;
                    ++scan;
                }

                if (VmPageFlags(page->vm.flags).Has(VmPageFlag::Dirty))
                    dom.dirtyList.PushBack(page); //TODO: queue dirty list pages for write-back
                else
                    dom.standbyList.PushBack(page);
                page->vm.flags = VmPageFlags(page->vm.flags).Set(VmPageFlag::Standby);

                const bool dirty = VmPageFlags(page->vm.flags).Has(VmPageFlag::Dirty);
                Log("VMD: 0x%tx unmapped, moved to %s list", LogLevel::Debug, PmRevLookup(page), dirty ? "dirty" : "standby");
            }
            dom.listsLock.Unlock();

            //TODO: send shootdowns in UnmapViewsOfPage() and then wait for a fence here (before the loop continues or wraps)

            if (result == WaitResult::Timeout)
                continue;

            //second stage: we woke because the event was signalled, indicating high memory pressure - free some.
            dom.listsLock.Lock();
            while (!dom.standbyList.Empty()) //TODO: some heuristic to determine how many pages to free-up
            {
                //page is not mapped anywhere, but it still exists in a vmo's list of pages, or overlay list.
                //we need to unlink it from those.
                auto page = dom.standbyList.PopFront();

                if (VmPageFlags(page->vm.flags).Has(VmPageFlag::IsOverlay))
                {
                    VmView& view = *static_cast<VmView*>(page->vm.vmo);

                    sl::ScopedLock viewLock(view.lock);

                    auto prev = view.overlay.End();
                    for (auto it = view.overlay.Begin(); it != view.overlay.End(); prev = it, ++it)
                    {
                        if (&*it != page)
                            continue;

                        if (prev == view.overlay.End())
                            view.overlay.PopFront();
                        else
                            view.overlay.EraseAfter(prev);
                        break;
                    }
                }
                else
                {
                    VmObject& vmo = *static_cast<VmObject*>(page->vm.vmo);
                    ASSERT_UNREACHABLE(); //TODO: handle this case
                }

                //page is now unmapped and unlinked, we can safely free it
                Log("VMD: 0x%tx unlinked, freeing!", LogLevel::Debug, PmRevLookup(page));
                PmFree(PmRevLookup(page));
            }
            dom.listsLock.Unlock();
        }

        ExitKernelThread(0);
    }

    bool Vmm::HandlePageFault(uintptr_t addr, VmFaultFlags flags)
    {
        viewsLock.ReaderLock();
        VmView* view = FindView(addr);
        if (view == nullptr)
        {
            viewsLock.ReaderUnlock();
            return false;
        }

        sl::ScopedLock viewLock(view->lock);
        viewsLock.ReaderUnlock();

        VALIDATE_(!flags.Has(VmFaultFlag::Fetch), false);
        VALIDATE_(!flags.Has(VmFaultFlag::User), false);

        const size_t offset = AlignDownPage(addr - view->base);
        if (!view->vmoRef.Valid() && flags.Has(VmFaultFlag::Read))
        {
            //read access on anon vmo, map the zero page as readonly
            const auto hatFlags = MakeHatFlags({}, hatMap != KernelMap());
            auto result = HatDoMap(hatMap, view->base + offset, domain->zeroPage, 0, hatFlags, false);
            return result == HatError::Success;
        }

        auto result = WirePage(*view, offset);
        if (result.HasError())
            return false;

        //WirePage() should probably be refactored, but for now the newly mapped page is wired,
        //so we want to decrement its wireCount so that it can be swapped as needed.
        Core::PageInfo* info = FindPageOfView(*view, offset);
        info->vm.wireCount -= 2;
        return true;
    }

    void Vmm::Activate()
    {
        if (this == &KernelVmm())
            return;

        ASSERT_UNREACHABLE(); //TODO: its a user VMM
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

        view->lock.Lock();
        view->base = *maybeBase + offsetBias;
        view->length = length;
        view->offset = offset;
        view->flags = flags;
        view->vmoRef = nullptr;
        view->key.backend = NoSwap;
        view->vmm = this;

        if (obj != nullptr)
        {
            view->vmoRef = &obj->refCount;

            sl::ScopedLock scopeLock(obj->lock);
            obj->views.PushBack(view);
        }

        view->lock.Unlock();
        viewsLock.WriterLock();
        views.InsertSorted(view, [](VmView* a, VmView* b) -> bool
            { return a->base < b->base; });
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

            sl::ScopedLock viewLock(view->lock);
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

            sl::ScopedLock viewLock(view->lock);
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
