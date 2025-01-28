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

    constexpr VmError MakeVmError(HatError err)
    {
        switch (err)
        {
        case HatError::Success:
            return VmError::Success;
        case HatError::PmAllocFailed:
            return VmError::TryAgain;
        default:
            return VmError::HatMapFailed;
        }
    }

    Core::PageInfo* Vmm::FindPageInList(sl::FwdList<Core::PageInfo, &Core::PageInfo::vmObjList>& list, size_t offsetPfn)
    {
        for (auto it = list.Begin(); it != list.End(); ++it)
        {
            if (offsetPfn < it->vm.offset)
                return nullptr;
            if (offsetPfn != it->vm.offset)
                continue;

            it->vm.wireCount++;

            domain->listsLock.Lock();
            if (VmPageFlags(it->vm.flags).Has(VmPageFlag::Standby))
            {
                auto& list = VmPageFlags(it->vm.flags).Has(VmPageFlag::Dirty) 
                    ? domain->dirtyList : domain->standbyList;
                
                auto prev = list.Begin();
                for (auto curr = list.Begin(); curr != list.End(); prev = curr, ++curr)
                {
                    if (&*curr != &*it)
                        continue;

                    if (prev == list.Begin())
                        list.PopFront();
                    else
                        list.EraseAfter(prev);
                    break;
                }

                domain->activeList.PushBack(&*it);
            }
            domain->listsLock.Unlock();

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

            HatCapabilities hatCaps {};
            HatGetCapabilities(hatCaps);

            view.vmm->hatLock.Lock();
            //if we have hardware-managed dirty bits, propogate that to the PageInfo struct
            if (hatCaps.hwDirtyBit)
            {
                auto result = HatGetDirty(view.vmm->hatMap, vaddr, true);
                ASSERT_(result.HasValue());
                if (result.Value())
                    page->vm.flags = VmPageFlags(page->vm.flags).SetThen(VmPageFlag::Dirty).Raw();
            }
            
            uintptr_t ignored0;
            size_t ignored1;
            HatDoUnmap(view.vmm->hatMap, vaddr, ignored0, ignored1);
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
                const bool evict = page->vm.wireCount == 0; //TODO: logic here (aging?)
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
                    ++scan;
                }

                if (VmPageFlags(page->vm.flags).Has(VmPageFlag::Dirty))
                    dom.dirtyList.PushBack(page);
                else
                    dom.standbyList.PushBack(page);
                page->vm.flags = VmPageFlags(page->vm.flags).SetThen(VmPageFlag::Standby).Raw();

                const bool dirty = VmPageFlags(page->vm.flags).Has(VmPageFlag::Dirty);
                Log("VMD: 0x%tx unmapped, moved to %s list", LogLevel::Debug, PmRevLookup(page), dirty ? "dirty" : "standby");
            }
            //TODO: send shootdowns in UnmapViewsOfPage() and then wait for a fence here (before the loop continues or wraps)

            //queue writeback ops for pages in the dirty list, when possible TODO:

            dom.listsLock.Unlock();
            if (result == WaitResult::Timeout && false)
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

    sl::ErrorOr<void, VmError> Vmm::HandleFault(uintptr_t addr, VmFaultType type)
    {
        viewsLock.ReaderLock();
        VmView* view = FindView(addr);
        if (view == nullptr)
        {
            viewsLock.ReaderUnlock();
            return VmError::InvalidArg;
        }

        sl::ScopedLock viewLock(view->lock);
        viewsLock.ReaderUnlock();

        Core::PageInfo* page = nullptr;
        const size_t offset = AlignDownPage(addr - view->base);
        const size_t offsetPfn = offset >> PfnShift();

        //check 1: does the overlay list contain an entry for the page we're interested in
        page = FindPageInList(view->overlay, offsetPfn);
        if (page != nullptr)
        {
            Log("VM: found 0x%tx in overlay list, mapping.", LogLevel::Debug, Core::PmRevLookup(page));
            const auto hatFlags = MakeHatFlags(view->flags, this != &KernelVmm());
            hatLock.Lock();
            const auto mapResult = HatDoMap(hatMap, view->base + offset, Core::PmRevLookup(page), 
                0, hatFlags);
            hatLock.Unlock();
            
            if (type != VmFaultType::Wire)
                page->vm.wireCount--;
            return MakeVmError(mapResult);
        }

        //check 2: does the view have a swap store associated with it, and can we get the page from there
        if (view->key.backend != NoSwap)
        {
            ASSERT_UNREACHABLE();
            //TODO: once the io manager is complete, use that to (async) interact with the swap backend
        }

        //check 3: can the backing store get us the page
        if (view->vmoRef.Valid())
        {
            VmObject* vmo = reinterpret_cast<VmObject*>(&view->vmoRef->count);
            sl::ScopedLock vmoScopeLock(vmo->lock);

            //3.1: is the page in the vmo's content list
            page = FindPageInList(vmo->content, offsetPfn);
            if (page != nullptr)
            {
                const auto hatFlags = MakeHatFlags(view->flags, this != &KernelVmm());
                hatLock.Lock();
                const auto mapResult = HatDoMap(hatMap, view->base + offset, Core::PmRevLookup(page), 
                    0, hatFlags);
                hatLock.Unlock();
                
                if (type != VmFaultType::Wire)
                    page->vm.wireCount--;
                return MakeVmError(mapResult);
            }

            //3.2: try get the page from the vmo pager
            if (vmo->isMmio)
            {
                const auto hatFlags = GetMmioVmoHatFlags(vmo, offset) 
                    | MakeHatFlags(view->flags, this != &KernelVmm());

                auto maybePaddr = GetMmioVmoPage(vmo, offset);
                if (!maybePaddr.HasValue())
                    return VmError::BadVmoOffset;

                hatLock.Lock();
                const auto mapResult = HatDoMap(hatMap, view->base + offset, *maybePaddr, 0,
                    hatFlags);
                hatLock.Unlock();

                return MakeVmError(mapResult);
            }
            else
                ASSERT_UNREACHABLE(); //TODO: implement the VFS and vnode pager!
        }
        //4: no vmo (meaning its anon memory), with no overlay or swap entries.
        else
        {
            if (type == VmFaultType::Read)
            {
                //for a read we can map the zero-page as readonly
                const auto hatFlags = MakeHatFlags({}, this != &KernelVmm());

                sl::ScopedLock scopeHatLock(hatLock);
                const auto result = HatDoMap(hatMap, view->base + offset, domain->zeroPage,
                    0, hatFlags);
                return MakeVmError(result);
            }
            else
            {
                //write or wire fault, unmap zero page if needed, map a zeroed page
                const auto hatFlags = MakeHatFlags(view->flags, this != &KernelVmm());

                sl::ScopedLock scopeLockHat(hatLock);
                size_t ignored0;
                if (auto mapped = HatGetMap(hatMap, view->base + offset, ignored0); mapped.HasValue())
                {
                    //unmap zero page
                    uintptr_t ignored1;
                    HatDoUnmap(hatMap, view->base + offset, ignored1, ignored0);
                }

                auto pmAlloc = Core::PmAlloc();
                if (!pmAlloc.HasValue())
                    return VmError::TryAgain;

                page = Core::PmLookup(*pmAlloc);
                sl::memset(reinterpret_cast<void*>(AddHhdm(*pmAlloc)), 0, PageSize());

                const auto result = HatDoMap(hatMap, view->base + offset, *pmAlloc, 0, 
                    hatFlags);
                scopeLockHat.Release();

                if (result != HatError::Success)
                {
                    Core::PmFree(*pmAlloc);
                    return MakeVmError(result);
                }

                page->vm.offset = offsetPfn;
                page->vm.flags = VmPageFlags(VmPageFlag::IsOverlay).Raw();
                page->vm.wireCount = type == VmFaultType::Wire ? 1 : 0;
                page->vm.vmo = view;

                view->overlay.InsertSorted(page, [](auto* a, auto* b) { return a->vm.offset < b->vm.offset; });

                domain->listsLock.Lock();
                domain->activeList.PushBack(page);
                domain->listsLock.Unlock();

                return VmError::Success;
            }
        }

        return VmError::InvalidArg;
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

        if (wire)
        {
            void* basePtr = reinterpret_cast<void*>(view->base);
            if (!WireRange(basePtr, view->length))
            {
                RemoveView(basePtr);
                return {};
            }
        }

        return reinterpret_cast<void*>(view->base);
    }
    
    void Vmm::RemoveView(void* base)
    {
        viewsLock.WriterLock();
        VmView* view = FindView(reinterpret_cast<uintptr_t>(base));
        if (view == nullptr)
        {
            viewsLock.WriterUnlock();
            return;
        }

        views.Remove(view);
        viewsLock.WriterUnlock();

        //TODO:
        // - ensure no one else is waiting to lock this view (how?)
        // - remove view from vmo's list, unref vmo
        // - unreserve swap space
        // - free overlay pages
        // - free vmview struct

        ASSERT_UNREACHABLE(); (void)base;
    }

    sl::Opt<void*> Vmm::SplitView(void* addr)
    { 
        const uintptr_t vaddr = reinterpret_cast<uintptr_t>(addr);

        viewsLock.ReaderLock();
        VmView* view = FindView(vaddr);
        if (view == nullptr)
        {
            viewsLock.ReaderUnlock();
            return {};
        }
        viewsLock.ReaderUnlock();

        sl::ScopedLock scopeLockView(view->lock);
        const size_t offset = vaddr - view->base;
        ASSERT_UNREACHABLE();
    }

    VmError Vmm::ChangeViewFlags(void* addr, VmViewFlags newFlags)
    { ASSERT_UNREACHABLE(); (void)addr; (void)newFlags; }

    bool Vmm::WireRange(void* base, size_t length)
    {
        const uintptr_t vaddr = reinterpret_cast<uintptr_t>(base);

        size_t wiredLength = 0;
        for (; wiredLength < length; wiredLength += PageSize())
        {
            const auto result = HandleFault(vaddr + wiredLength, VmFaultType::Wire);
            if (result.HasError())
                break;
        }

        if (wiredLength < length) //couldnt wire the entire range, undo what we did earlier
            UnwireRange(base, wiredLength);

        return wiredLength >= length;
    }

    void Vmm::UnwireRange(void* base, size_t length)
    {
        const uintptr_t vaddr = reinterpret_cast<uintptr_t>(base);

        for (size_t i = 0; i < length; i += PageSize())
        {
            size_t ignored0;
            auto result = HatGetMap(hatMap, vaddr + i, ignored0);
            if (result.HasValue())
                Core::PmLookup(*result)->vm.wireCount--;
        }
    }

    bool Vmm::AcquireMdl(void* base, size_t length, npk_mdl* mdl)
    {
        if (!WireRange(base, length))
            return false;

        const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(base);
        const size_t baseOffset = baseAddr & PageMask();
        const uintptr_t alignedBaseAddr = AlignDownPage(baseAddr);
        const size_t entryCount = (AlignUpPage(length - (PageSize() - baseOffset)) >> PfnShift()) + 1;

        uintptr_t* entries = static_cast<uintptr_t*>(Core::WiredAlloc(entryCount * sizeof(uintptr_t)));
        if (entries == nullptr)
        {
            UnwireRange(base, length);
            return false;
        }

        for (size_t i = 0; i < entryCount; i++)
        {
            size_t ignored0;
            auto maybeMap = HatGetMap(hatMap, alignedBaseAddr + (i << PfnShift()), ignored0);
            if (!maybeMap.HasValue())
            {
                UnwireRange(base, length);
                Core::WiredFree(entries, entryCount * sizeof(uintptr_t));
                return false;
            }

            entries[i] = *maybeMap;
        }

        mdl->virt_base = base;
        mdl->length = length;
        mdl->entries = entries;
        mdl->entry0_offset = baseOffset;

        return true;
    }

    void Vmm::ReleaseMdl(npk_mdl* mdl)
    {
        VALIDATE_(mdl != nullptr, );
        VALIDATE_(mdl->entries != nullptr, );

        UnwireRange(mdl->virt_base, mdl->length);

        const size_t entryCount = (AlignUpPage(mdl->length - (PageSize() - mdl->entry0_offset)) >> PfnShift()) + 1;
        Core::WiredFree(mdl->entries, entryCount * sizeof(uintptr_t));
        mdl->entries = nullptr;
    }

    Vmm kernelVmm;
    Vmm& KernelVmm()
    {
        return kernelVmm;
    }
}
