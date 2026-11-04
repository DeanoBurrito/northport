#pragma once

#include "Types.hpp"
#include "Atomic.hpp"
#include "Compiler.hpp"

namespace sl
{
    /* Counter is odd while writes are in progress. A reader that observes an
     * odd value or a different value before and after reading the synchornized
     * data should retry.
     * Writers must serialize between themselves if this is required.
     */
    class SeqCount
    {
    private:
        sl::Atomic<uint32_t> version;
        uint32_t depth;
        uint32_t entryVersion;

    public:
        constexpr SeqCount()
            : version(0), depth(0), entryVersion(0)
        {}

        SeqCount(const SeqCount&) = delete;
        SeqCount& operator=(const SeqCount&) = delete;
        SeqCount(SeqCount&&) = delete;
        SeqCount& operator=(SeqCount&&) = delete;

        inline void BeginWrite()
        {
            if (depth++ != 0)
                return;

            entryVersion = version.Load(sl::Relaxed);
            version.Store(entryVersion + 1, sl::Relaxed);

            sl::AtomicThreadFence(sl::Release);
        }

        inline void EndWrite()
        {
            if (--depth != 0)
                return;

            version.Store(entryVersion + 2, sl::Release);
        }

        [[nodiscard]]
        inline uint32_t BeginRead() const
        {
            return version.Load(sl::Acquire);
        }

        /* Returns whether a read token (from `BeginRead()`) needs to retry.
         */
        [[nodiscard]]
        inline bool ReadRetry(uint32_t start) const
        {
            if (start & 1)
                return true;

            sl::AtomicThreadFence(sl::Acquire);

            return version.Load(sl::Relaxed) != start;
        }

        [[nodiscard]]
        inline bool WriteInProgress() const
        {
            return version.Load(sl::Relaxed) & 1;
        }
    };

    class ScopedSeqWrite
    {
    private:
        SeqCount& seq;

    public:
        inline ScopedSeqWrite(SeqCount& target)
            : seq(target)
        {
            seq.BeginWrite();
        }

        inline ~ScopedSeqWrite()
        {
            seq.EndWrite();
        }

        ScopedSeqWrite(const ScopedSeqWrite&) = delete;
        ScopedSeqWrite& operator=(const ScopedSeqWrite&) = delete;
        ScopedSeqWrite(ScopedSeqWrite&&) = delete;
        ScopedSeqWrite& operator=(ScopedSeqWrite&&) = delete;
    };

    template<typename Fn>
    inline bool SeqRead(const SeqCount& seq, Fn reader, size_t maxAttempts = 16)
    {
        for (size_t attempt = 0; attempt < maxAttempts; attempt++)
        {
            const auto start = seq.BeginRead();
            if (start & 1)
            {
                HintSpinloop();
                continue;
            }

            reader();

            if (!seq.ReadRetry(start))
                return true;

            HintSpinloop();
        }

        return false;
    }
}
