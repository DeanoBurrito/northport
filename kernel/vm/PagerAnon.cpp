#include <private/Vm.hpp>
#include <lib/PlacementNew.hpp>

/* Anon objects are very similar to an amap except they present as a VmSource,
 * so we get that class of operations available too.
 */
namespace Npk::Private
{
    static void FreeAnonSourceNow(EbrItem* item)
    {
        auto* anon = reinterpret_cast<AnonVmSource*>(item);
        PoolFreeWired(anon, sizeof(*anon), VmSourceTag);
    }

    static bool AnonRefObj(VmSource* src, PagerFlags flags)
    {
        (void)flags;

        NPK_ASSERT(src != nullptr);
        auto* anon = static_cast<AnonVmSource*>(src);

        return sl::IncrementRefCount<AnonVmSource, &AnonVmSource::refs>(anon);
    }

    static void AnonUnrefObj(VmSource* src)
    {
        NPK_ASSERT(src != nullptr);
        auto* anon = static_cast<AnonVmSource*>(src);

        if (!sl::DecrementRefCount<AnonVmSource, &AnonVmSource::refs>(anon))
            return;

        DestroySource(*src, FreeAnonSourceNow);
    }

    static NpkStatus AnonGet(VmSource* src, sl::Span<PageInfo*> pages,
        size_t pagesOffset, size_t mainIndex, VmPagerMeta* meta,
        PagerFlags flags)
    {
        (void)meta;
        (void)flags;

        NPK_CHECK(src != nullptr, NpkStatus::InvalidArg);
        NPK_CHECK(mainIndex < pages.Size(), NpkStatus::InvalidArg);

        auto* anon = static_cast<AnonVmSource*>(src);
        const size_t limit = anon->length >> PfnShift();

        auto mainResult = NpkStatus::NotAvailable;
        for (size_t i = 0; i < pages.Size(); i++)
        {
            auto* page = pages[i];
            if (page == nullptr)
                continue;

            //pages are already zeroed before handing to `Get()`, so no actual
            //work to do here, just the bounds check.
            auto result = NpkStatus::BadVaddr;
            if (pagesOffset + i < limit)
            {
                PageSetFlag(page, PageVmFlag::Filled);
                result = NpkStatus::Success;
            }

            if (i == mainIndex)
                mainResult = result;
        }

        return mainResult;
    }

    constexpr VmPagerOps anonPagerOps
    {
        .RefObj = AnonRefObj,
        .UnrefObj = AnonUnrefObj,
        .Get = AnonGet,
        .Fault = nullptr,
        .Put = nullptr,
        .Flush = nullptr,
        .Release = nullptr,
    };

    VmSource* AnonSourceAttach(size_t size)
    {
        NPK_CHECK(size != 0, nullptr);

        void* ptr = PoolAllocWired(sizeof(AnonVmSource), VmSourceTag);
        if (ptr == nullptr)
            return nullptr;

        auto* source = new(ptr) AnonVmSource {};
        ResetBook(source->book, BookKind::Source, BookBacking::Swap);

        source->ops = &anonPagerOps;
        source->length = AlignUpPage(size);
        source->refs = 1;

        return source;
    }
}
