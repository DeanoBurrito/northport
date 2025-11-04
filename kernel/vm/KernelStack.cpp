#include <VmPrivate.hpp>
#include <Core.hpp>
#include <Magazine.hpp>

namespace Npk
{
    constexpr HeapTag StackMagTag = NPK_MAKE_HEAP_TAG("StkM");

    constexpr size_t KernelStackMagDepth = 8;
    constexpr size_t StackDepoPreferredSize = 2;
    constexpr size_t KernelStackDepoTrimCount = 64;

    using KernelStackMag = sl::Magazine<void*>;
    using KernelStackList = sl::FwdList<KernelStackMag, &KernelStackMag::hook>;

    constexpr size_t KernelStackMagSize = sizeof(KernelStackMag) + sizeof(void*)
        * KernelStackMagDepth;

    static IplSpinLock<Ipl::Passive> kernelStackDepoLock;
    static KernelStackList kernelStacksDepoFullMags;
    static KernelStackList kernelStacksDepoEmptyMags;
    static size_t kernelStacksDepoAccessCount;

    static void DestroyStackMag(KernelStackMag* mag)
    {
        using namespace Private;

        NPK_ASSERT(mag->count == KernelStackMagDepth);

        const HwMap map = MyKernelMap();

        for (size_t i = 0; i < mag->count; i++)
        {
            Paddr paddr {};
            uintptr_t vaddr = (uintptr_t)mag->items[i];

            while (true)
            {
                auto status = ClearMap(map, vaddr, &paddr);

                //nothing was mapped, we're done with this stack
                if (status == VmStatus::BadVaddr)
                    break;

                //unexpected error, gtfo
                NPK_CHECK(status == VmStatus::Success, );

                auto page = LookupPageInfo(paddr);
                NPK_ASSERT(page != nullptr);
                FreePage(page);

                vaddr -= PageSize();
            }
            vaddr += PageSize();

            //release the address space used by the stack
            const uintptr_t base = (uintptr_t)mag->items[i] - (KernelStackSize()
                + PageSize());
            const size_t length = (uintptr_t)mag->items[i] + PageSize();
            FreeInSpace(MyKernelSpace(), (void*)base, length);
            //TODO: shootdown if required
        }

        HeapFreeNonPaged(mag, KernelStackMagSize);
    }

    static KernelStackMag* CreateStackMag(bool full)
    {
        void* magPtr = HeapAllocNonPaged(KernelStackMagSize, StackMagTag);
        if (magPtr == nullptr)
            return nullptr;

        KernelStackMag* mag = static_cast<KernelStackMag*>(magPtr);
        mag->count = 0;
        if (!full)
            return mag;

        bool abort = false;
        for (size_t i = 0; i < KernelStackMagDepth; i++)
        {
            //we unconditionally leave guard pages around kernel stacks, see
            //the extra 2 pages allocated here.
            void* addr = nullptr;
            auto status = AllocateInSpace(MyKernelSpace(), &addr, 
                KernelStackSize() + (2 << PfnShift()), 1);

            if (status != VmStatus::Success)
                abort = true;

            for (size_t p = 0; !abort && p < KernelStackPages(); p++)
            {
                const uintptr_t vaddr = (uintptr_t)addr + (p << PfnShift())
                    + PageSize();
                auto page = AllocPage(true);

                if (page != nullptr)
                {
                    auto status = SetKernelMap(vaddr, LookupPagePaddr(page), 
                        VmFlag::Write | VmFlag::Bound);

                    if (status != VmStatus::Success)
                    {
                        FreePage(page);
                        abort = true;
                    }
                }

                //undo partially mapped stack
                if (abort)
                {
                    for (size_t q = 0; q < p; q++)
                    {
                        Paddr paddr = 0;
                        uintptr_t vaddr = (uintptr_t)addr + (q << PfnShift());
                        auto status = ClearKernelMap(vaddr, &paddr);

                        if (status != VmStatus::Success)
                        {
                            Log("Failure unmapping kernel stack page @ 0x%tx",
                                LogLevel::Error, vaddr);
                        }
                        else
                            FreePage(LookupPageInfo(paddr));
                    }

                    break;
                }
            }

            if (abort)
            {
                FreeInSpace(MyKernelSpace(), addr, KernelStackSize());
                mag->count = i;
                break;
            }

            addr = (void*)((uintptr_t)addr + KernelStackSize());
            mag->items[i] = addr;
        }

        if (abort)
        {
            DestroyStackMag(mag);
            return nullptr;
        }
        
        return mag;
    }

    static void TrimKernelStackDepo()
    {
        sl::ScopedLock scopeLock(kernelStackDepoLock);

        size_t count = 0;
        for (auto it = kernelStacksDepoFullMags.Begin(); 
            it != kernelStacksDepoFullMags.End(); ++it)
            count++;

        while (count > StackDepoPreferredSize)
        {
            auto mag = kernelStacksDepoFullMags.PopFront();
            DestroyStackMag(mag);
            count--;
        }

        count = 0;
        for (auto it = kernelStacksDepoEmptyMags.Begin();
            it != kernelStacksDepoEmptyMags.End(); ++it)
            count++;

        while (count > StackDepoPreferredSize)
        {
            auto mag = kernelStacksDepoEmptyMags.PopFront();
            DestroyStackMag(mag);
            count--;
        }
    }

    static void KernelStackDepoExchange(KernelStackMag** mag, size_t size)
    {
        (void)size;

        NPK_CHECK(mag != nullptr, );
        NPK_CHECK(*mag != nullptr, );

        bool trimDepo = false;
        if ((*mag)->count == 0)
        {
            KernelStackMag* swap = nullptr;

            kernelStackDepoLock.Lock();
            if (!kernelStacksDepoFullMags.Empty())
                swap = kernelStacksDepoFullMags.PopFront();

            kernelStacksDepoAccessCount++;
            if (kernelStacksDepoAccessCount == KernelStackDepoTrimCount)
            {
                kernelStacksDepoAccessCount = 0;
                trimDepo = true;
            }
            kernelStacksDepoEmptyMags.PushBack(*mag);
            kernelStackDepoLock.Unlock();

            if (swap == nullptr)
                swap = CreateStackMag(true);

            NPK_ASSERT(swap != nullptr);
            *mag = swap;
        }
        else
        {
            NPK_CHECK((*mag)->count == KernelStackMagDepth, );

            KernelStackMag* swap = nullptr;

            kernelStackDepoLock.Lock();
            if (!kernelStacksDepoEmptyMags.Empty())
                swap = kernelStacksDepoEmptyMags.PopFront();
            kernelStacksDepoFullMags.PushBack(*mag);
            kernelStackDepoLock.Unlock();

            if (swap == nullptr)
                swap = CreateStackMag(false);
        
            NPK_ASSERT(swap != nullptr);
            *mag = swap;
        }

        if (trimDepo)
            TrimKernelStackDepo();
    }

    static void KernelStackDepoInit(KernelStackMag** full, 
        KernelStackMag** empty, size_t size)
    {
        (void)size;

        NPK_CHECK(full != nullptr, );
        NPK_CHECK(empty != nullptr, );

        *full = CreateStackMag(true);
        if (*full == nullptr)
            return;

        *empty = CreateStackMag(false);
        if (*empty == nullptr)
        {
            DestroyStackMag(*full);
            *full = nullptr;
        }
    }

    using KernelStackCache = sl::MagazineCache<void*, KernelStackMagDepth, 
        KernelStackDepoExchange, KernelStackDepoInit>;

    CPU_LOCAL(KernelStackCache, static kernelStackCache);

    VmStatus AllocKernelStack(void** stack)
    {
        void* allocated = kernelStackCache->Alloc();
        if (allocated == nullptr)
            return VmStatus::Shortage;

        *stack = allocated;
        return VmStatus::Success;
    }

    void FreeKernelStack(void* stack)
    {
        NPK_CHECK(stack != nullptr, );

        kernelStackCache->Free(&stack);
    }
}
