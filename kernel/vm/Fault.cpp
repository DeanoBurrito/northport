#include <private/Vm.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    constexpr size_t MaxPageFaultAttempts = 4;

    static size_t NextAmapSlotCount(size_t baseIndex, size_t max)
    {
        baseIndex++;
        baseIndex += baseIndex / 2;

        return sl::Min(baseIndex, max);
    }

    static NpkStatus TryCompletePageFault(VmSpace& space, uintptr_t addr, 
        bool write)
    {
        VmRange* range;
        auto result = SpaceLookup(&range, space, addr);
        if (result != NpkStatus::Success)
            return result;

        VmFlags flags {};
        if (range->flags.Has(VmFlag::Mmio))
            flags.Set(VmFlag::Mmio);
        if (range->flags.Has(VmFlag::Fetch))
            flags.Set(VmFlag::Fetch);

        const size_t amapSlot = (addr - range->base) >> PfnShift();
        Paddr paddr {};

        if (write)
        {
            if (!range->flags.Has(VmFlag::Write))
                return NpkStatus::NotWritable;

            flags.Set(VmFlag::Write);

            const bool isCow = range->flags.Has(VmFlag::CopyOnWrite);

            //first we do some sanity checks about the state of the range and
            //it's amap: does it have/need one, is it exclusively owned?

            if (isCow && !range->amapRef.Valid())
            {
                //this range doesnt have an amap but it needs one as it CoWable.

                AnonMap* mapPtr;
                const size_t slotCount = NextAmapSlotCount(amapSlot, 
                    range->length >> PfnShift());

                result = Private::CreateAnonMap(&mapPtr, slotCount);
                if (result != NpkStatus::Success)
                    return result;

                range->amapOffset = 0;
                range->amapRef = mapPtr;
                mapPtr->refcount--;
            }
            else if (isCow && range->flags.Has(VmFlag::AmapNeedsCopy))
            {
                //we have an amap but it's shared with another range, duplicate
                //it so we have a private amap we can modify.
                NPK_ASSERT(range->amapRef.Valid());

                AnonMapRef newMapRef {};
                result = Private::AnonMapClone(&newMapRef, *range->amapRef);

                if (result != NpkStatus::Success)
                    return result;
                NPK_ASSERT(newMapRef.Valid());

                range->amapRef = newMapRef;
                range->flags.Clear(VmFlag::AmapNeedsCopy);
            }

            if (isCow && range->amapRef->slotCount <= amapSlot)
            {
                //we have an amap but its too small!

                const size_t slotCount = NextAmapSlotCount(amapSlot, 
                    range->length >> PfnShift());

                result = Private::ResizeAnonMap(*range->amapRef, slotCount);
                if (result != NpkStatus::Success)
                    return result;
            }

            //amap state is fine, now we try to get a page to map: first we
            //try the amap layer, then the source layer (if a source object is
            //attached to this range). If there's no source there are two paths
            //from there, either the range is CoW (its plain anonymous memory)
            //or it's not (nothing should be mapped here, this range is dead
            //space).

            result = NpkStatus::NotAvailable;
            if (range->amapRef.Valid())
            {
                auto ref = Private::AnonMapLookup(*range->amapRef, amapSlot);

                if (ref.Valid())
                {
                    PageInfo* page;
                    result = Private::AnonPageGetPage(&page, ref);
                    if (result != NpkStatus::Success)
                        return result;

                    paddr = LookupPagePaddr(page);
                    result = NpkStatus::Success;
                }
            }

            if (result != NpkStatus::Success && range->source != nullptr)
            {
                //TODO:
                if (isCow)
                    NPK_UNREACHABLE(); //add page to amap, copied from source
                else
                    NPK_UNREACHABLE(); //map directly to source page.
            }

            if (result != NpkStatus::Success && range->source == nullptr 
                && isCow)
            {
                //there's no backing object and this object is CoW, so we'll
                //allocate a fresh page and insert it into the amap.

                auto page = AllocPage(false);
                paddr = LookupPagePaddr(page);

                AnonPage* anon;
                result = Private::CreateAnonPage(&anon);
                if (result != NpkStatus::Success)
                    return result;

                //the lock isn't necessary here, I'm using it for ordering.
                anon->lock.Lock();
                anon->page = page;
                anon->lock.Unlock();
                page->vm.refcount++; //TODO: make atomic!
                AnonPageRef ref = anon;

                result = Private::AnonMapAdd(*range->amapRef, amapSlot, ref);
                if (result != NpkStatus::Success)
                {
                    ref = {};
                    anon->refcount--;
                    anon->page = nullptr;

                    Private::DestroyAnonPage(anon);
                    FreePage(page);
                }
            }

            if (result == NpkStatus::Success)
            {
                //we have something to map, check if there's something already
                //mapped in this space: handle any derefencing appropriately.

                constexpr Paddr InvalidPaddr = -1;
                Paddr prevPaddr = InvalidPaddr;
                result = ClearMap(space.map, addr, &prevPaddr);

                if (result == NpkStatus::Success)
                {
                    //TODO: there was something mapped here before, handle that.
                    //How we handle it depends on the layer, and is kind of 
                    //the inverse of the 'read' path (see below).
                    //If the page came an amap: copy the anon page and decrement its
                    //refcount, or if the refcount is already 1, make it writable
                    //since its now ours anyway.
                    //If the page came from the source layer and the range is not
                    //CoW, ensure its writable (this shouldnt happen honestly).
                    //If it is CoW, we need to make a copy of the page into the
                    //anon layer, then we can de-ref the page via the source layer.
                    //If the page didnt come from the amap layer and there is no
                    //source, it must be the zero-page (we can assert this, and then
                    //do nothing).
                    NPK_ASSERT(prevPaddr == MySystemDomain().zeroPage);
                }

                result = NpkStatus::Success;
            }
        }
        else //if: (read access)
        {
            result = NpkStatus::InternalError;
            if (range->amapRef.Valid())
            {
                auto ref = Private::AnonMapLookup(*range->amapRef, amapSlot);

                if (ref.Valid())
                {
                    PageInfo* page;
                    result = Private::AnonPageGetPage(&page, ref);
                    if (result != NpkStatus::Success)
                        return result;

                    paddr = LookupPagePaddr(page);
                    result = NpkStatus::Success;
                }
            }

            if (result != NpkStatus::Success && range->source != nullptr)
            { NPK_UNREACHABLE(); } //TODO: implement

            if (result != NpkStatus::Success 
                && range->flags.Has(VmFlag::CopyOnWrite))
            {
                paddr = MySystemDomain().zeroPage;
                result = NpkStatus::Success;
            }
        }

        if (result != NpkStatus::Success)
            return result;

        addr = AlignDownPage(addr);
        result = SetMap(space.map, addr, paddr, flags);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            NPK_UNREACHABLE();
        }

        return result;
    }

    void DispatchPageFault(uintptr_t addr, bool write, bool user)
    {
        LowerIpl(Ipl::Passive);
        const bool prevIntrs = IntrsOn();

        const size_t topBit = 1ull << ((sizeof(addr) * 8) - 1);
        const bool userAddr = !(addr & topBit);

        NpkStatus result = NpkStatus::Success;
        VmSpace* space = nullptr;
        if (user)
        {
            //TODO: when we have signals replace the calls to NPK_UNREACHABLE()
            //with sending a signal about this thread.
            if (!userAddr)
                NPK_UNREACHABLE();
            if (!HwIsCanonicalUserAddress(addr))
                NPK_UNREACHABLE();

            //TODO: get active user address space
        }
        else
        {
            if (userAddr)
            {
                Panic("Kernel attempted to directly %s user memory at %p",
                    nullptr, write ? "write" : "read", addr);
            }

            space = MySystemDomain().kernelSpace;
        }
        NPK_ASSERT(space != nullptr);

        for (size_t i = 0; i < MaxPageFaultAttempts || userAddr; i++)
        {
            result = TryCompletePageFault(*space, addr, write);
            if (result == NpkStatus::Success)
                break;
        }

        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            NPK_UNREACHABLE();
        }

        IntrsExchange(prevIntrs);
        RaiseIpl(Ipl::Interrupt);
    }
}
