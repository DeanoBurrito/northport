#include <private/Vm.hpp>
#include <Io.hpp>
#include <lib/Maths.hpp>

namespace Npk::Private
{
    constexpr auto MemSwapTag = NPK_MAKE_HEAP_TAG("MSwp");

    constexpr size_t DefaultSegmentPages = 64;

    struct MemSwapStore
    {
        SwapStore* store;
        IoInterface io;

        Paddr base;
        size_t pages;
        uint8_t* metadata; //see `HwPageMetadataSize()`

        size_t delayUs;
        size_t jitterUs;
        uint64_t rngState;
        IplSpinLock<Ipl::Dpc> queueLock;
        IopQueue queue;
        ClockEvent timer;
        bool timerArmed;
    };

    static MemSwapStore memSwap;
    static WorkItem memSwapWork;

    //NOTE: assumes `queueLock` is held, so seeds can be reproduced
    static size_t NextDelay(MemSwapStore& self)
    {
        if (self.jitterUs == 0)
            return self.delayUs;

        uint64_t x = self.rngState; //xorshift64
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.rngState = x;

        return self.delayUs + (x % (self.jitterUs + 1));
    }

    static NpkStatus CopySlot(MemSwapStore& self, PageInfo* page, size_t index,
        bool toStore)
    {
        NPK_ASSERT(index < self.pages);

        const Paddr offset = static_cast<Paddr>(index) << PfnShift();
        const Paddr slotPaddr = self.base + offset;
        const auto prevIpl = EnsureIpl(Ipl::Dpc);

        auto pageWindow = AccessPage(page);
        auto slotWindow = AccessPage(slotPaddr);

        const bool valid = pageWindow.Valid() && slotWindow.Valid();
        if (valid)
        {
            if (toStore)
                sl::MemCopy(slotWindow.vaddr, pageWindow.vaddr, PageSize());
            else 
                sl::MemCopy(pageWindow.vaddr, slotWindow.vaddr, PageSize());
        }

        DestroyPageAccess(&pageWindow);
        DestroyPageAccess(&slotWindow);
        RestoreIpl(prevIpl);

        if (!valid)
            return NpkStatus::Shortage;
        return NpkStatus::Success;
    }

    static void CopySlotMetadata(MemSwapStore& self, uint8_t* packetMetadata,
        size_t buffer, size_t index, bool toStore)
    {
        const size_t stride = HwPageMetadataSize();

        if (stride == 0 || packetMetadata == nullptr)
            return;
        if (self.metadata == nullptr)
            return;

        uint8_t* inPacket = packetMetadata + (buffer * stride);
        uint8_t* inStore = self.metadata + (index * stride);

        if (toStore)
            sl::MemCopy(inStore, inPacket, stride);
        else
            sl::MemCopy(inPacket, inStore, stride);
    }

    static NpkStatus DoTransfer(MemSwapStore& self, Iop* iop)
    {
        auto buffers = GetIopBuffers(iop);
        const size_t count = buffers.Size();
        const auto& params = GetIopParams(iop).readwrite;
        const uint64_t first = params.offset;

        if (first >= self.pages || count > self.pages - first)
            return NpkStatus::InvalidArg;

        const bool toStore = GetIopType(iop) == IoType::Write;
        auto* metadata = static_cast<uint8_t*>(params.metadata);

        for (size_t i = 0; i < count; i++)
        {
            auto& buffer = buffers[i];

            if (buffer.type != IoBufferType::PageList
                || buffer.physical.pages.Size() != 1)
            {
                return NpkStatus::InvalidArg;
            }

            const size_t index = static_cast<size_t>(first) + i;
            auto result = CopySlot(self, &buffer.physical.pages[0], index,
                toStore);

            if (result != NpkStatus::Success)
                return result;

            CopySlotMetadata(self, metadata, i, index, toStore);
            ReduceIopResidual(iop, 1);
        }

        return NpkStatus::Success;
    }

    //NOTE: assumes `queueLock` is held.
    static NpkStatus ArmTimer(MemSwapStore& self, size_t delayUs)
    {
        if (self.timerArmed)
            return NpkStatus::Success;

        const auto expiry = GetMonotonicTime()
            + sl::TimeCount(sl::Micros, delayUs);

        Completion completion {};
        completion.Set(&memSwapWork, CompletionType::WorkItem);

        auto result = ResetClockEvent(&self.timer, expiry, {}, completion);
        if (result != NpkStatus::Success)
            return result;

        result = AddClockEvent(&self.timer);
        if (result != NpkStatus::Success)
            return result;

        self.timerArmed = true;

        return NpkStatus::Success;
    }

    static void MemSwapDrain(WorkItem* item, void* arg)
    {
        (void)item;

        auto& self = *static_cast<MemSwapStore*>(arg);

        self.queueLock.Lock();
        self.timerArmed = false;
        self.queueLock.Unlock();

        while (true)
        {
            self.queueLock.Lock();
            auto* packet = DequeueIop(self.queue);
            self.queueLock.Unlock();

            if (packet == nullptr)
                break;

            AnswerIop(packet, DoTransfer(self, packet));
        }
    }

    static NpkStatus MemSwapTryComplete(Iop* iop, void* opaque, void* stash)
    {
        (void)opaque;
        (void)stash;

        return TakeIopAnswer(iop);
    }

    static NpkStatus MemSwapBegin(Iop* iop, void* opaque, void** stash)
    {
        (void)stash;
        auto& self = *static_cast<MemSwapStore*>(opaque);

        const auto type = GetIopType(iop);
        if (type != IoType::Read && type != IoType::Write)
            return NpkStatus::InvalidArg;

        if (self.delayUs == 0 && self.jitterUs == 0)
            return DoTransfer(self, iop);

        self.queueLock.Lock();
        const auto result = ArmTimer(self, NextDelay(self));
        if (result == NpkStatus::Success)
            EnqueueIop(self.queue, iop);
        self.queueLock.Unlock();

        if (result == NpkStatus::Success)
            return NpkStatus::Pending;

        return result;
    }

    bool MemSwapStoreEnabled()
    {
        return ReadConfigUint("npk.memswap.enable", false);
    }

    NpkStatus InitMemorySwapStore(Paddr base, size_t pages)
    {
        if (!MemSwapStoreEnabled())
            return NpkStatus::BadConfig;
        if (base == 0 || pages == 0)
            return NpkStatus::InvalidArg;

        const size_t delay = ReadConfigUint("npk.memswap.delay_us", 50);
        const size_t jitter = ReadConfigUint("npk.memswap.jitter_us", 25);
        const uint64_t seed = ReadConfigUint("npk.memswap.rng_seed",
            0x9E3779B97F4A7C15);
        const size_t segPages = ReadConfigUint("npk.memswap.segment_pages", 
            DefaultSegmentPages);
        const size_t metaBytes = pages * HwPageMetadataSize();

        auto result = ResetWorkItem(&memSwapWork, MemSwapDrain, &memSwap);
        if (result != NpkStatus::Success)
            return result;

        if (pages < segPages)
            return NpkStatus::Shortage;
        if (metaBytes != 0)
        {
            void* ptr = PoolAllocWired(metaBytes, MemSwapTag);
            if (ptr == nullptr)
                return NpkStatus::Shortage;

            memSwap.metadata = static_cast<uint8_t*>(ptr);
        }

        memSwap.base = base;
        memSwap.pages = pages;
        memSwap.delayUs = delay;
        memSwap.jitterUs = jitter;
        memSwap.rngState = sl::Max<uint64_t>(seed, 1);
    
        memSwap.io.parent = nullptr;
        memSwap.io.opaque = &memSwap;
        memSwap.io.Begin = MemSwapBegin;
        memSwap.io.TryComplete = MemSwapTryComplete;
        memSwap.io.End = nullptr;
        memSwap.io.refcount = 1;

        SwapStoreDesc desc {};
        desc.io = &memSwap.io;
        desc.name = "mem-swap"_span;
        desc.priority = 0;
        desc.capacityPages = pages;
        desc.segmentPages = sl::Max<size_t>(1, segPages);

        result = RegisterSwapStore(&memSwap.store, desc);
        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);
            
            if (metaBytes != 0)
                PoolFreeWired(memSwap.metadata, metaBytes, MemSwapTag);
            memSwap.metadata = nullptr;

            return result;
        }

        Log("In-memory swap store init: base=0x%tx, pages=%zu, delay=%zuus, "
            "jitter=%zuus, seed=0x%tx", LogLevel::Info, base, pages, delay,
            jitter, seed);

        return NpkStatus::Success;
    }
}
