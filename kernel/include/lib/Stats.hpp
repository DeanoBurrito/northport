#pragma once

#include "Atomic.hpp"
#include "Memory.hpp"
#include "Compiler.hpp"
#include "Span.hpp"

namespace sl
{
    template<typename Selector, size_t Count, typename CountType = uint64_t>
    struct Stats
    {
    private:
        constexpr static size_t DefaultCopyAttempts = 16;

        union
        {
            //this is a uint32_t for portability reasons: uint64_t on 32-bit
            //targets may result in a call to a compiler lib + a hashed lock.
            //That's not nice and I dont need the full 64-bit space for this
            //field, so this is my preferred solution.
            sl::Atomic<uint32_t> version = {};
            CountType padding;
        };

        CountType values[Count] = {};

        uint32_t depth = 0;
        uint32_t entryVersion = 0;

        void BeginWrite()
        {
            if (depth++ != 0)
                return;

            entryVersion = version.Load(sl::Relaxed);
            version.Store(entryVersion + 1, sl::Relaxed);

            sl::AtomicThreadFence(sl::Release);
        }

        void EndWrite()
        {
            if (--depth != 0)
                return;

            version.Store(entryVersion + 2, sl::Release);
        }

    public:
        void Reset()
        {
            BeginWrite();
            for (size_t i = 0; i < Count; i++)
                values[i] = 0;
            EndWrite();
        }

        void Add(Selector what, size_t count)
        {
            BeginWrite();
            values[static_cast<size_t>(what)] += count;
            EndWrite();
        }

        void Sub(Selector what, size_t count)
        {
            BeginWrite();
            values[static_cast<size_t>(what)] -= count;
            EndWrite();
        }

        void Set(Selector what, size_t value)
        {
            BeginWrite();
            values[static_cast<size_t>(what)] = value;
            EndWrite();
        }

        bool Copy(Stats& dest, size_t maxAttempts = DefaultCopyAttempts) const
        {
            for (size_t attempt = 0; attempt < maxAttempts; attempt++)
            {
                const auto startVersion = version.Load(sl::Acquire);

                if (startVersion & 1)
                {
                    HintSpinloop();
                    continue;
                }

                sl::MemCopy(&dest, this, sizeof(*this));
                sl::AtomicThreadFence(sl::Acquire);

                if (version.Load(sl::Relaxed) == startVersion)
                    return true;

                HintSpinloop();
            }

            return false;
        }

        //returns access to stat counters, only useful on copied-to stats.
        sl::Span<const CountType> GetCounters() const
        {
            return values;
        }
    };
}
