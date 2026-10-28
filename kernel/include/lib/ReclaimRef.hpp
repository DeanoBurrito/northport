#pragma once

#include "Types.hpp"
#include "Atomic.hpp"
#include "Compiler.hpp"

namespace sl
{
    /* Similar to `sl::Ref<T>` but zero is a valid state and 'dead' is a
     * separate state. So a reference with no holders can be reclaimed but it is
     * still sticky at zero.
     */
    class ReclaimRef
    {
    private:
        constexpr static uint32_t DeadValue = ~static_cast<uint32_t>(0);

        sl::Atomic<uint32_t> count;

    public:
        constexpr ReclaimRef()
            : count(0)
        {}

        constexpr explicit ReclaimRef(uint32_t initial)
            : count(initial)
        {}

        ReclaimRef(const ReclaimRef&) = delete;
        ReclaimRef& operator=(const ReclaimRef&) = delete;
        ReclaimRef(ReclaimRef&&) = delete;
        ReclaimRef& operator=(ReclaimRef&&) = delete;

        [[nodiscard]]
        bool Acquire()
        {
            uint32_t current = count.Load(sl::Relaxed);

            while (true)
            {
                if (current == DeadValue)
                    return false;
                if (count.CompareExchange(current, current + 1, sl::Acquire))
                    return true;

                sl::HintSpinloop();
            }
        }

        uint32_t AcquireLocked()
        {
            return count.FetchAdd(1, sl::Acquire);
        }

        [[nodiscard]]
        static constexpr uint32_t DeadMarker()
        {
            return DeadValue;
        }

        [[nodiscard]]
        bool Release()
        {
            if (count.FetchSub(1, sl::Release) != 1)
                return false;
            sl::AtomicThreadFence(sl::Acquire);

            return true;
        }

        [[nodiscard]]
        bool Claim()
        {
            uint32_t expected = 0;

            return count.CompareExchange(expected, DeadValue, sl::Acquire);
        }

        [[nodiscard]]
        bool IsDead() const
        {
            return count.Load(sl::Relaxed) == DeadValue;
        }

        [[nodiscard]]
        uint32_t Load() const
        {
            return count.Load(sl::Relaxed);
        }
    };
}
