#pragma once

namespace sl
{
    enum class MemoryOrder
    {
        SeqCst = __ATOMIC_SEQ_CST,
        Acquire = __ATOMIC_ACQUIRE,
        Release = __ATOMIC_RELEASE,
        AcqRel = __ATOMIC_ACQ_REL,
        Relaxed = __ATOMIC_RELAXED,
    };

    constexpr inline MemoryOrder SeqCst = MemoryOrder::SeqCst;
    constexpr inline MemoryOrder Acquire = MemoryOrder::Acquire;
    constexpr inline MemoryOrder Release = MemoryOrder::Release;
    constexpr inline MemoryOrder AcqRel = MemoryOrder::AcqRel;
    constexpr inline MemoryOrder Relaxed = MemoryOrder::Relaxed;
    
    template<typename T>
    class Atomic
    {
    private:
        constexpr static unsigned minAlignment = 
            (sizeof(T) & (sizeof(T) - 1)) || sizeof(T) > 16 ? 0 : sizeof(T);
        
        constexpr static unsigned alignment = minAlignment > alignof(T) ? minAlignment : alignof(T);

        alignas(alignment) T value {};

        static_assert(__is_trivially_copyable(T), "Bad atomic type, must be trivally copyable");

    public:
        constexpr Atomic() : value{} 
        {}

        ~Atomic() = default;
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) volatile = delete;

        constexpr Atomic(T initialValue) : value(initialValue)
        {}

        operator T() const
        { return Load(); }

        operator T() const volatile
        { return Load(); }

        T operator=(T incoming)
        { Store(incoming); return value; }

        T operator=(T incoming) volatile
        { Store(incoming); return value; }

        bool IsLockFree() const
        { 
            return __atomic_is_lock_free(sizeof(value), reinterpret_cast<void*>(-alignment)); 
        }

        bool IsLockFree() const volatile
        { 
            return __atomic_is_lock_free(sizeof(value), reinterpret_cast<void*>(-alignment)); 
        }

        void Store(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_store_n(&value, incoming, (int)order); }

        void Store(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_store_n(&value, incoming, (int)order); }

        T Load(MemoryOrder order = MemoryOrder::SeqCst) const
        { return __atomic_load_n(&value, (int)order); }

        T Load(MemoryOrder order = MemoryOrder::SeqCst) const volatile
        { return __atomic_load_n(&value, (int)order); }

        T Exchange(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_exchange_n(&value, incoming, (int)order); }

        T Exchange(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_exchange_n(&value, incoming, (int)order); }

        T operator++(int)
        { return FetchAdd(1); }

        T operator++(int) volatile
        { return FetchAdd(1); }

        T operator--(int)
        { return FetchSub(1); }

        T operator--(int) volatile
        { return FetchSub(1); }

        T operator++()
        { return __atomic_add_fetch(&value, 1, (int)MemoryOrder::SeqCst); }

        T operator++() volatile
        { return __atomic_add_fetch(&value, 1, (int)MemoryOrder::SeqCst); }

        T operator--()
        { return __atomic_sub_fetch(&value, 1, (int)MemoryOrder::SeqCst); }

        T operator+=(T incoming)
        { return __atomic_add_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator+=(T incoming) volatile
        { return __atomic_add_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator-=(T incoming)
        { return __atomic_sub_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator-=(T incoming) volatile
        { return __atomic_sub_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator&=(T incoming)
        { return __atomic_and_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator&=(T incoming) volatile
        { return __atomic_and_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator|=(T incoming)
        { return __atomic_or_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T operator|=(T incoming) volatile
        { return __atomic_or_fetch(&value, incoming, (int)MemoryOrder::SeqCst); }

        T FetchAdd(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_fetch_add(&value, incoming, (int)order); }

        T FetchAdd(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_fetch_add(&value, incoming, (int)order); }

        T FetchSub(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_fetch_sub(&value, incoming, (int)order); }

        T FetchSub(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_fetch_sub(&value, incoming, (int)order); }

        T FetchAnd(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_fetch_and(&value, incoming, (int)order); }

        T FetchAnd(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_fetch_and(&value, incoming, (int)order); }

        T FetchOr(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_fetch_or(&value, incoming, (int)order); }

        T FetchOr(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_fetch_or(&value, incoming, (int)order); }

        T FetchXor(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { return __atomic_fetch_xor(&value, incoming, (int)order); }

        T FetchXor(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { return __atomic_fetch_xor(&value, incoming, (int)order); }

        void Add(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_fetch_add(&value, incoming, (int)order); }

        void Add(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_fetch_add(&value, incoming, (int)order); }

        void Sub(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_fetch_sub(&value, incoming, (int)order); }

        void Sub(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_fetch_sub(&value, incoming, (int)order); }

        void And(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_fetch_and(&value, incoming, (int)order); }

        void And(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_fetch_and(&value, incoming, (int)order); }

        void Or(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_fetch_or(&value, incoming, (int)order); }

        void Or(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_fetch_or(&value, incoming, (int)order); }

        void Xor(T incoming, MemoryOrder order = MemoryOrder::SeqCst)
        { __atomic_fetch_xor(&value, incoming, (int)order); }

        void Xor(T incoming, MemoryOrder order = MemoryOrder::SeqCst) volatile
        { __atomic_fetch_xor(&value, incoming, (int)order); }
    };
}
