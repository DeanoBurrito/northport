#include <private/Vm.hpp>
#include <lib/Maths.hpp>
#include <lib/Memory.hpp>

/* This file implements a VM pager that uses the bootloader module as a backing
 * store. This was for testing purposes, I've left it in the codebase because
 * there might be some real world use for it someday.
 */
namespace Npk::Private
{
    constexpr auto ModulePagerTag = NPK_MAKE_HEAP_TAG("TstP");

    struct ModuleVmSource : public VmSource
    {
        sl::RefCount refs;
        size_t length;

        Mutex writeMutex;
        void* writeBack;
        size_t writeBackPages;

        sl::SpinLock injectLock;
        uint64_t rng;
        size_t failPercent;
        size_t maxDelayUs;
    };

    static ModuleVmSource* moduleVmSource;
    static Paddr moduleBlobBase;
    static size_t moduleBlobLen;

    static uint64_t NextRandom(ModuleVmSource& src)
    {
        src.injectLock.Lock();

        uint64_t x = src.rng;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        src.rng = x;

        src.injectLock.Unlock();

        return x * 0x2545F4914F6CDD1DULL;
    }

    static void DoRandomDelay(ModuleVmSource& src)
    {
        if (src.maxDelayUs == 0)
            return;

        const uint64_t roll = NextRandom(src);
        const size_t micros = roll % (src.maxDelayUs + 1);
        if (micros == 0)
            return;

        Condition sleeper {};
        if (ResetCondition(&sleeper, 1) != NpkStatus::Success)
            return;

        WaitEntry entry {};
        const auto status = WaitOne(&sleeper, &entry,
            sl::TimeCount(sl::TimeScale::Micros, micros), NPK_WAIT_LOCATION);

        NPK_ASSERT(status == NpkStatus::Timeout);
    }

    static bool GetRandomFailure(ModuleVmSource& src)
    {
        if (src.failPercent == 0)
            return false;

        const uint64_t roll = NextRandom(src);

        return (roll % 100) < src.failPercent;
    }

    static NpkStatus ModulePagerFillPage(ModuleVmSource& src, PageInfo* page,
        size_t sourcePage)
    {
        const size_t byteOffset = sourcePage << PfnShift();
        if (byteOffset >= src.length)
            return NpkStatus::BadVaddr;

        size_t fromBlob = 0;
        if (byteOffset < moduleBlobLen)
            fromBlob = sl::Min(PageSize(), moduleBlobLen - byteOffset);

        const auto prevIpl = EnsureIpl(Ipl::Dpc);
        auto dest = AccessPage(page);
        if (!dest.Valid())
        {
            RestoreIpl(prevIpl);

            return NpkStatus::Shortage;
        }

        auto* destBytes = static_cast<char*>(dest.vaddr);
        if (fromBlob != 0)
        {
            auto blob = AccessPage(moduleBlobBase + byteOffset);
            if (!blob.Valid())
            {
                RestoreIpl(prevIpl);

                return NpkStatus::Shortage;
            }

            sl::MemCopy(destBytes, blob.vaddr, fromBlob);
        }

        if (fromBlob < PageSize())
            sl::MemSet(destBytes + fromBlob, 0, PageSize() - fromBlob);

        RestoreIpl(prevIpl);

        return NpkStatus::Success;
    }

    static NpkStatus ModulePagerStorePage(ModuleVmSource& src, PageInfo* page,
        size_t sourcePage)
    {
        if ((sourcePage << PfnShift()) >= src.length)
            return NpkStatus::BadVaddr;

        auto result = AcquireMutex(&src.writeMutex, sl::NoTimeout,
            NPK_WAIT_LOCATION);
        if (result != NpkStatus::Success)
            return result;

        if (src.writeBack == nullptr)
        {
            const size_t pages = src.length >> PfnShift();

            src.writeBack = PoolAllocWired(pages << PfnShift(),
                ModulePagerTag);
            if (src.writeBack == nullptr)
            {
                ReleaseMutex(&src.writeMutex);

                return NpkStatus::Shortage;
            }

            src.writeBackPages = pages;
            sl::MemSet(src.writeBack, 0, pages << PfnShift());
        }

        if (sourcePage >= src.writeBackPages)
        {
            ReleaseMutex(&src.writeMutex);

            return NpkStatus::BadVaddr;
        }

        auto* dest = static_cast<char*>(src.writeBack)
            + (sourcePage << PfnShift());

        const auto prevIpl = EnsureIpl(Ipl::Dpc);
        auto srcRef = AccessPage(page);
        if (!srcRef.Valid())
        {
            RestoreIpl(prevIpl);
            ReleaseMutex(&src.writeMutex);

            return NpkStatus::Shortage;
        }

        sl::MemCopy(dest, srcRef.vaddr, PageSize());

        RestoreIpl(prevIpl);
        ReleaseMutex(&src.writeMutex);

        return NpkStatus::Success;
    }

    static bool ModuleRefObj(VmSource* source, PagerFlags flags)
    {
        (void)flags;

        NPK_CHECK(source != nullptr, false);
        auto* src = static_cast<ModuleVmSource*>(source);

        return sl::IncrementRefCount<ModuleVmSource, &ModuleVmSource::refs>(src);
    }

    static void ModuleUnrefObj(VmSource* source)
    {
        NPK_ASSERT(source != nullptr);
        auto* src = static_cast<ModuleVmSource*>(source);

        if (!sl::DecrementRefCount<ModuleVmSource, &ModuleVmSource::refs>(src))
            return;
        //else: last reference is gone, cleanup.

        Private::DrainSourceCache(*source);
        if (src->writeBack != nullptr)
        {
            PoolFreeWired(src->writeBack, src->writeBackPages << PfnShift(),
                ModulePagerTag);
        }

        moduleVmSource = nullptr;
        PoolFreeWired(src, sizeof(*src), VmSourceTag);
    }

    static NpkStatus ModuleGet(VmSource* source, sl::Span<PageInfo*> pages,
        size_t pagesOffset, size_t mainIndex, PagerFlags flags)
    {
        (void)flags;

        NPK_CHECK(source != nullptr, NpkStatus::InvalidArg);
        NPK_CHECK(mainIndex < pages.Size(), NpkStatus::InvalidArg);

        auto* src = static_cast<ModuleVmSource*>(source);

        DoRandomDelay(*src);

        auto mainResult = NpkStatus::NotAvailable;
        for (size_t i = 0; i < pages.Size(); i++)
        {
            auto* page = pages[i];

            //from UVM: nullptr indicates the caller doesnt want the slot.
            if (page == nullptr)
                continue;

            NpkStatus result;
            if (GetRandomFailure(*src))
                result = NpkStatus::NotAvailable;
            else
                result = ModulePagerFillPage(*src, page, pagesOffset + i);

            if (result == NpkStatus::Success)
            {
                page->vm.offset = static_cast<uint32_t>(pagesOffset + i);
                page->vm.flags.Set(PageVmFlag::Filled);
            }

            if (i == mainIndex)
                mainResult = result;
        }

        return mainResult;
    }

    static NpkStatus ModulePut(VmSource* source, sl::Span<PageInfo*> pages,
        size_t pagesOffset, PagerFlags flags)
    {
        (void)flags;

        NPK_CHECK(source != nullptr, NpkStatus::InvalidArg);
        auto* src = static_cast<ModuleVmSource*>(source);

        DoRandomDelay(*src);

        auto result = NpkStatus::Success;
        for (size_t i = 0; i < pages.Size(); i++)
        {
            auto* page = pages[i];
            if (page == nullptr)
                continue;

            if (GetRandomFailure(*src))
            {
                result = NpkStatus::NotAvailable;
                continue;
            }

            const auto status = ModulePagerStorePage(*src, page,
                pagesOffset + i);

            if (status != NpkStatus::Success)
                result = status;
            else
                page->vm.flags.Set(PageVmFlag::Clean);
        }

        return result;
    }

    static void ModuleFlush(VmSource* source, size_t offsetPages,
        size_t lengthPages, PagerFlags flags)
    {
        (void)flags;

        NPK_ASSERT(source != nullptr);
        auto* src = static_cast<ModuleVmSource*>(source);

        if (AcquireMutex(&src->writeMutex, sl::NoTimeout, NPK_WAIT_LOCATION)
            != NpkStatus::Success)
            return;

        if (src->writeBack != nullptr)
        {
            const size_t first = sl::Min(offsetPages, src->writeBackPages);
            const size_t last = sl::Min(offsetPages + lengthPages,
                src->writeBackPages);

            if (last > first)
            {
                auto* base = static_cast<char*>(src->writeBack)
                    + (first << PfnShift());

                sl::MemSet(base, 0, (last - first) << PfnShift());
            }
        }

        ReleaseMutex(&src->writeMutex);
    }

    constexpr VmPagerOps modulePagerOps
    {
        .RefObj = ModuleRefObj,
        .UnrefObj = ModuleUnrefObj,
        .Get = ModuleGet,
        .Fault = nullptr,
        .Put = ModulePut,
        .Flush = ModuleFlush,
        .Release = nullptr,
    };

    VmSource* ModuleSourceAttach()
    {
        if (!ModuleSourceAvailable())
            return nullptr;

        return moduleVmSource;
    }

    bool ModuleSourceAvailable()
    {
        return moduleBlobBase != 0 && moduleBlobLen != 0;
    }

    NpkStatus InitModuleSource(uint64_t seed, size_t failPercent,
        size_t maxDelayUs)
    {
        auto result = GetLoaderModule(moduleBlobBase, moduleBlobLen);
        if (result != NpkStatus::Success)
            return result;

        //not really necessary but its a consistency point between now and later
        if (!ModuleSourceAvailable())
            return NpkStatus::NotAvailable;

        Log("Initializing bootloader module VmSource: 0x%tx, %zu bytes",
            LogLevel::Verbose, moduleBlobBase, moduleBlobLen);

        void* ptr = PoolAllocWired(sizeof(ModuleVmSource), VmSourceTag);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        auto* source = new(ptr) ModuleVmSource {};
        result = ResetSxMutex(&source->mutex);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(ptr, sizeof(*source), VmSourceTag);

            return result;
        }

        result = ResetMutex(&source->writeMutex, 1);
        if (result != NpkStatus::Success)
        {
            PoolFreeWired(ptr, sizeof(*source), VmSourceTag);

            return result;
        }

        source->ops = &modulePagerOps;
        source->rng = seed;
        source->failPercent = sl::Min(failPercent, (size_t)100);
        source->maxDelayUs = maxDelayUs;
        moduleVmSource = source;
        Log("Bootloader module VmSource available.", LogLevel::Info);

        return NpkStatus::Success;
    }
}
