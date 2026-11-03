#pragma once

#include "CppUtils.hpp"
#include "Span.hpp"

namespace sl
{
    struct FwdListHook
    {
        void* next;
    };

    template<typename T, FwdListHook T::*Hook>
    class FwdList
    {
    private:
        T* head;
        T* tail;

    public:
        static FwdListHook* Hk(T* value)
        { 
            return &(value->*Hook);
        }

        class Iterator
        {
        friend FwdList;
        private:
            T* ptr;

            constexpr Iterator(T* entry) 
                : ptr(entry)
            {}

        public:
            constexpr bool operator==(const Iterator& other) const
            { 
                return ptr == other.ptr;
            }

            constexpr bool operator!=(const Iterator& other) const
            { 
                return ptr != other.ptr;
            }

            constexpr Iterator& operator++()
            {
                ptr = static_cast<T*>(Hk(ptr)->next);
                return *this;
            }

            constexpr T& operator*()
            { 
                return *ptr;
            }

            constexpr T* operator->()
            { 
                return ptr;
            }

            constexpr const T& operator*() const
            { 
                return *ptr;
            }

            constexpr const T* operator->() const
            { 
                return ptr;
            }
        };

        constexpr FwdList() 
            : head(nullptr), tail(nullptr)
        {}

        FwdList(const FwdList&) = delete;
        FwdList& operator=(const FwdList&) = delete;
        FwdList(FwdList&&) = delete;
        FwdList& operator=(FwdList&&) = delete;

        T& Front() const
        { 
            return *head;
        }

        T& Back() const
        { 
            return *tail;
        }

        Iterator Begin()
        { 
            return head;
        }

        Iterator End()
        { 
            return nullptr;
        }

        const Iterator Begin() const
        { 
            return head;
        }

        const Iterator End() const
        { 
            return nullptr;
        }

        bool Empty() const
        { 
            return head == nullptr;
        }

        void PushFront(T* value)
        {
            Hk(value)->next = head;
            head = value;
            if (tail == nullptr)
                tail = head;
        }

        void PushBack(T* value)
        {
            Hk(value)->next = nullptr;
            if (Empty())
                head = value;
            else
                Hk(tail)->next = value;
            tail = value;
        }

        T* PopFront()
        {
            if (head == nullptr)
                return nullptr;

            T* temp = head;
            head = static_cast<T*>(Hk(head)->next);

            if (head == nullptr)
                tail = nullptr;

            return temp;
        }

        void Splice(FwdList& other)
        {
            if (other.Empty())
                return;

            if (Empty())
                head = other.head;
            else
                Hk(tail)->next = other.head;

            tail = other.tail;
            other.head = nullptr;
            other.tail = nullptr;
        }

        void InsertAfter(Iterator it, T* value)
        {
            if (it == End())
                return PushBack(value);

            Hk(value)->next = Hk(it.ptr)->next;
            Hk(it.ptr)->next = value;
            
            if (it.ptr == tail)
                tail = value;
        }

        Iterator EraseAfter(Iterator it)
        {
            if (it == End())
                return nullptr;

            T* temp = static_cast<T*>(Hk(it.ptr)->next);
            if (temp != nullptr)
                Hk(it.ptr)->next = Hk(temp)->next;
            else
                Hk(it.ptr)->next = nullptr;

            if (temp == tail)
            {
                tail = static_cast<T*>(Hk(temp)->next);
                if (tail == nullptr)
                    tail = it.ptr;
            }

            return temp;
        }

        void InsertSorted(T* value, bool (*LessThan)(T* a, T* b))
        {
            if (Empty() || LessThan(value, head))
                return PushFront(value);

            if (LessThan(tail, value))
                return PushBack(value);

            auto it = Begin();
            while (it != End())
            {
                auto prev = it;
                ++it;
                if (it == End() || LessThan(value, &*it))
                    return InsertAfter(prev, value);
            }
        }

        void Exchange(FwdList& other)
        {
            sl::Swap(other.head, head);
            sl::Swap(other.tail, tail);
        }
    };

    struct ListHook
    {
        void* prev;
        void* next;
    };

    template<typename T, ListHook T::*Hook>
    class List
    {
    private:
        T* head;
        T* tail;

    public:
        static ListHook* Hk(T* value)
        { 
            return &(value->*Hook);
        }

        class Iterator
        {
        friend List;
        private:
            T* ptr;

            constexpr Iterator(T* entry) 
                : ptr(entry)
            {}

        public:
            constexpr bool operator==(const Iterator& other) const
            { 
                return ptr == other.ptr;
            }

            constexpr bool operator!=(const Iterator& other) const
            { 
                return ptr != other.ptr;
            }

            constexpr Iterator& operator++()
            {
                ptr = static_cast<T*>(Hk(ptr)->next);

                return *this;
            }

            constexpr T& operator*()
            { 
                return *ptr;
            }

            constexpr T* operator->()
            { 
                return ptr;
            }

            constexpr const T& operator*() const
            { 
                return *ptr;
            }

            constexpr const T* operator->() const
            { 
                return ptr;
            }
        };

        constexpr List() 
            : head(nullptr), tail(nullptr)
        {}

        List(const List&) = delete;
        List& operator=(const List&) = delete;
        List(List&&) = delete;
        List& operator=(List&&) = delete;

        T& Front()
        { 
            return *head;
        }

        T& Back()
        { 
            return *tail;
        }

        Iterator Begin()
        { 
            return head;
        }

        Iterator End()
        { 
            return nullptr;
        }

        Iterator After(T* value)
        { 
            return static_cast<T*>(Hk(value)->next);
        }

        Iterator Before(T* value)
        { 
            return static_cast<T*>(Hk(value)->prev);
        }

        const Iterator After(T* value) const
        { 
            return static_cast<T*>(Hk(value)->next);
        }

        const Iterator Before(T* value) const
        { 
            return static_cast<T*>(Hk(value)->prev);
        }

        const Iterator Begin() const
        { 
            return head;
        }

        const Iterator End() const
        { 
            return nullptr;
        }

        bool Empty() const
        { 
            return head == nullptr;
        }

        void PushFront(T* value)
        {
            Hk(value)->prev = nullptr;
            Hk(value)->next = head;

            if (head == nullptr)
                tail = value;
            else
                Hk(head)->prev = value;
            head = value;
        }

        void PushBack(T* value)
        {
            Hk(value)->next = nullptr;
            Hk(value)->prev = tail;

            if (tail == nullptr)
                head = value;
            else
                Hk(tail)->next = value;
            tail = value;
        }

        T* PopFront()
        {
            if (head == nullptr)
                return nullptr;

            T* temp = head;
            head = static_cast<T*>(Hk(head)->next);

            if (head == nullptr)
                tail = nullptr;
            else
                Hk(head)->prev = nullptr;

            return temp;
        }

        T* PopBack()
        {
            if (tail == nullptr)
                return nullptr;

            T* temp = tail;
            tail = static_cast<T*>(Hk(tail)->prev);

            if (tail == nullptr)
                head = nullptr;
            else
                Hk(tail)->next = nullptr;

            return temp;
        }

        void InsertAfter(Iterator it, T* value)
        {
            if (it.ptr == nullptr)
                return PushBack(value);
            else
            {
                Hk(value)->prev = it.ptr;
                Hk(value)->next = Hk(it.ptr)->next;
                Hk(it.ptr)->next = value;
            }

            if (Hk(value)->next == nullptr)
                tail = value;
            else
                Hk(static_cast<T*>(Hk(value)->next))->prev = value;
        }

        void InsertBefore(Iterator it, T* value)
        {
            Hk(value)->next = it.ptr;

            if (it.ptr == nullptr)
                return PushBack(value);
            else
            {
                Hk(value)->prev = Hk(it.ptr)->prev;
                Hk(it.ptr)->prev = value;

                if (Hk(value)->prev == nullptr)
                    head = value;
                else
                    Hk(static_cast<T*>(Hk(value)->prev))->next = value;
            }
        }

        void InsertSorted(T* value, bool (*LessThan)(T* a, T* b))
        {
            if (Empty() || LessThan(value, head))
                return PushFront(value);
            if (LessThan(tail, value))
                return PushBack(value);
                
            for (auto it = Begin(); it != End(); ++it)
            {
                if (LessThan(value, &*it))
                    return InsertBefore(it, value);
            }

            PushBack(value);
        }

        Iterator Remove(T* value)
        {
            if (value == head)
            {
                PopFront();
                return Begin();
            }
            else if (value == tail)
            {
                PopBack();
                return End();
            }

            auto prev = static_cast<T*>(Hk(value)->prev);
            auto next = static_cast<T*>(Hk(value)->next);

            Hk(prev)->next = next;
            Hk(next)->prev = prev;
            Hk(value)->prev = nullptr;
            Hk(value)->next = nullptr;

            return next;
        }

        Iterator Remove(Iterator it)
        { 
            return Remove(it.ptr);
        }

        void Exchange(List& other)
        {
            sl::Swap(other.head, head);
            sl::Swap(other.tail, tail);
        }
    };

    template<typename Index>
    constexpr Index IndexListNull = static_cast<Index>(~static_cast<Index>(0));

    template<typename Index, Index NullIndex = IndexListNull<Index>>
    struct IndexFwdListHook
    {
        Index next = NullIndex;
    };

    template<typename T, typename IndexType>
    class IndexSpanStorage
    {
    private:
        static_assert(IsUnsigned<IndexType>, "IndexType must be unsigned");

        T* store;
        size_t count;

    public:
        using Index = IndexType;
        static constexpr Index Null = IndexListNull<Index>;

        constexpr IndexSpanStorage()
            : store(nullptr), count(0)
        {}

        [[nodiscard]]
        bool Adopt(sl::Span<T> storage)
        {
            if (storage.Size() > static_cast<size_t>(Null))
                return false;

            store = storage.Size() == 0 ? nullptr : &storage[0];
            count = storage.Size();

            return true;
        }

        [[nodiscard]]
        sl::Span<T> Elements() const
        {
            return sl::Span<T>(store, count);
        }

        [[nodiscard]]
        Index IndexOf(const T* value) const
        {
            if (value == nullptr || store == nullptr)
                return Null;
            if (value < store || value >= store + count)
                return Null;

            return static_cast<Index>(value - store);
        }

        [[nodiscard]]
        T* At(Index index) const
        {
            if (index == Null || static_cast<size_t>(index) >= count)
                return nullptr;

            return store + index;
        }

        T* Entry(Index index) const
        {
            return store + index;
        }

        bool SameAs(const IndexSpanStorage& other) const
        {
            return store == other.store && count == other.count;
        }
    };

    template<typename T, typename Index, auto Hook,
        typename Backing = IndexSpanStorage<T, Index>>
    class IndexFwdList : private Backing
    {
    private:
        static_assert(IsSame<typename Backing::Index, Index>,
            "An index list's storage must index by the list's index type.");
        static_assert(IsSame<decltype(Hook),
            IndexFwdListHook<Index, Backing::Null> T::*>,
            "An index list's hook must carry its storage's end marker.");

        Index head;
        Index tail;

        const Backing& Store() const
        {
            return *this;
        }

    public:
        static constexpr Index Null = Backing::Null;

        static IndexFwdListHook<Index, Null>* Hk(T* value)
        {
            return &(value->*Hook);
        }

        static void ResetHook(T* value)
        {
            Hk(value)->next = Null;
        }

        class Iterator
        {
        friend IndexFwdList;
        private:
            Backing store;
            Index index;

            constexpr Iterator(const Backing& storage, Index entry)
                : store(storage), index(entry)
            {}

        public:
            constexpr bool operator==(const Iterator& other) const
            {
                return index == other.index;
            }

            constexpr bool operator!=(const Iterator& other) const
            {
                return index != other.index;
            }

            constexpr Iterator& operator++()
            {
                index = Hk(store.Entry(index))->next;
                return *this;
            }

            constexpr Index CurrentIndex() const
            {
                return index;
            }

            constexpr T& operator*() const
            {
                return *store.Entry(index);
            }

            constexpr T* operator->() const
            {
                return store.Entry(index);
            }
        };

        constexpr IndexFwdList()
            : Backing(), head(Null), tail(Null)
        {}

        IndexFwdList(const IndexFwdList&) = delete;
        IndexFwdList& operator=(const IndexFwdList&) = delete;
        IndexFwdList(IndexFwdList&&) = delete;
        IndexFwdList& operator=(IndexFwdList&&) = delete;

        [[nodiscard]]
        bool Reset(sl::Span<T> storage)
        {
            if (!Backing::Adopt(storage))
                return false;

            head = Null;
            tail = Null;

            return true;
        }

        [[nodiscard]]
        sl::Span<T> Storage() const
        {
            return Store().Elements();
        }

        [[nodiscard]]
        Index IndexOf(const T* value) const
        {
            return Store().IndexOf(value);
        }

        [[nodiscard]]
        T* At(Index index) const
        {
            return Store().At(index);
        }

        [[nodiscard]]
        T* Front() const
        {
            return At(head);
        }

        [[nodiscard]]
        T* Back() const
        {
            return At(tail);
        }

        Iterator Begin() const
        {
            return Iterator(Store(), head);
        }

        Iterator End() const
        {
            return Iterator(Store(), Null);
        }

        bool Empty() const
        {
            return head == Null;
        }

        [[nodiscard]]
        bool PushFront(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null)
                return false;

            Hk(value)->next = head;
            head = index;
            if (tail == Null)
                tail = head;

            return true;
        }

        [[nodiscard]]
        bool PushBack(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null)
                return false;

            Hk(value)->next = Null;
            if (Empty())
                head = index;
            else
                Hk(Store().Entry(tail))->next = index;
            tail = index;

            return true;
        }

        T* PopFront()
        {
            if (head == Null)
                return nullptr;

            T* temp = Store().Entry(head);
            head = Hk(temp)->next;

            if (head == Null)
                tail = Null;

            Hk(temp)->next = Null;
            return temp;
        }

        [[nodiscard]]
        bool InsertAfter(Iterator it, T* value)
        {
            if (it == End())
                return PushBack(value);

            const Index index = IndexOf(value);
            if (index == Null)
                return false;

            T* after = Store().Entry(it.index);
            Hk(value)->next = Hk(after)->next;
            Hk(after)->next = index;

            if (it.index == tail)
                tail = index;

            return true;
        }

        T* EraseAfter(Iterator it)
        {
            if (it == End())
                return nullptr;

            T* prev = Store().Entry(it.index);
            const Index index = Hk(prev)->next;
            if (index == Null)
                return nullptr;

            T* temp = Store().Entry(index);
            Hk(prev)->next = Hk(temp)->next;

            if (index == tail)
                tail = it.index;

            Hk(temp)->next = Null;
            return temp;
        }

        [[nodiscard]]
        bool Remove(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null || Empty())
                return false;

            if (head == index)
                return PopFront() == value;

            for (auto it = Begin(); it != End(); ++it)
            {
                if (Hk(&*it)->next == index)
                    return EraseAfter(it) == value;
            }

            return false;
        }

        [[nodiscard]]
        bool InsertSorted(T* value, bool (*LessThan)(T* a, T* b))
        {
            if (IndexOf(value) == Null)
                return false;

            if (Empty() || LessThan(value, Store().Entry(head)))
                return PushFront(value);

            if (LessThan(Store().Entry(tail), value))
                return PushBack(value);

            auto it = Begin();
            while (it != End())
            {
                auto prev = it;
                ++it;
                if (it == End() || LessThan(value, &*it))
                    return InsertAfter(prev, value);
            }

            return PushBack(value);
        }

        [[nodiscard]]
        bool Exchange(IndexFwdList& other)
        {
            if (!Store().SameAs(other.Store()))
                return false;

            sl::Swap(other.head, head);
            sl::Swap(other.tail, tail);

            return true;
        }
    };

    template<typename Index, Index NullIndex = IndexListNull<Index>>
    struct IndexListHook
    {
        Index prev = NullIndex;
        Index next = NullIndex;
    };

    template<typename T, typename Index, auto Hook,
        typename Backing = IndexSpanStorage<T, Index>>
    class IndexList : private Backing
    {
    private:
        static_assert(IsSame<typename Backing::Index, Index>);
        static_assert(IsSame<decltype(Hook),
            IndexListHook<Index, Backing::Null> T::*>);

        Index head;
        Index tail;

        const Backing& Store() const
        {
            return *this;
        }

        bool LinkedAt(Index index, const T* value) const
        {
            const auto* hook = &(value->*Hook);
            return hook->prev != Null || hook->next != Null || head == index;
        }

    public:
        static constexpr Index Null = Backing::Null;

        static IndexListHook<Index, Null>* Hk(T* value)
        {
            return &(value->*Hook);
        }

        static void ResetHook(T* value)
        {
            Hk(value)->prev = Null;
            Hk(value)->next = Null;
        }

        class Iterator
        {
        friend IndexList;
        private:
            Backing store;
            Index index;

            constexpr Iterator(const Backing& storage, Index entry)
                : store(storage), index(entry)
            {}

        public:
            constexpr bool operator==(const Iterator& other) const
            {
                return index == other.index;
            }

            constexpr bool operator!=(const Iterator& other) const
            {
                return index != other.index;
            }

            constexpr Iterator& operator++()
            {
                index = Hk(store.Entry(index))->next;
                return *this;
            }

            constexpr Iterator& operator--()
            {
                index = Hk(store.Entry(index))->prev;
                return *this;
            }

            constexpr Index CurrentIndex() const
            {
                return index;
            }

            constexpr T& operator*() const
            {
                return *store.Entry(index);
            }

            constexpr T* operator->() const
            {
                return store.Entry(index);
            }
        };

        constexpr IndexList()
            : Backing(), head(Null), tail(Null)
        {}

        IndexList(const IndexList&) = delete;
        IndexList& operator=(const IndexList&) = delete;
        IndexList(IndexList&&) = delete;
        IndexList& operator=(IndexList&&) = delete;

        [[nodiscard]]
        bool Reset(sl::Span<T> storage)
        {
            if (!Backing::Adopt(storage))
                return false;

            head = Null;
            tail = Null;

            return true;
        }

        [[nodiscard]]
        sl::Span<T> Storage() const
        {
            return Store().Elements();
        }

        [[nodiscard]]
        Index IndexOf(const T* value) const
        {
            return Store().IndexOf(value);
        }

        [[nodiscard]]
        T* At(Index index) const
        {
            return Store().At(index);
        }

        [[nodiscard]]
        bool IsLinked(const T* value) const
        {
            const Index index = IndexOf(value);
            if (index == Null)
                return false;

            return LinkedAt(index, value);
        }

        [[nodiscard]]
        T* Front() const
        {
            return At(head);
        }

        [[nodiscard]]
        T* Back() const
        {
            return At(tail);
        }

        Iterator Begin() const
        {
            return Iterator(Store(), head);
        }

        Iterator End() const
        {
            return Iterator(Store(), Null);
        }

        Iterator After(T* value) const
        {
            return Iterator(Store(), Hk(value)->next);
        }

        Iterator Before(T* value) const
        {
            return Iterator(Store(), Hk(value)->prev);
        }

        bool Empty() const
        {
            return head == Null;
        }

        [[nodiscard]]
        bool PushFront(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null || LinkedAt(index, value))
                return false;

            Hk(value)->prev = Null;
            Hk(value)->next = head;

            if (head == Null)
                tail = index;
            else
                Hk(Store().Entry(head))->prev = index;
            head = index;

            return true;
        }

        [[nodiscard]]
        bool PushBack(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null || LinkedAt(index, value))
                return false;

            Hk(value)->next = Null;
            Hk(value)->prev = tail;

            if (tail == Null)
                head = index;
            else
                Hk(Store().Entry(tail))->next = index;
            tail = index;

            return true;
        }

        T* PopFront()
        {
            if (head == Null)
                return nullptr;

            T* temp = Store().Entry(head);
            head = Hk(temp)->next;

            if (head == Null)
                tail = Null;
            else
                Hk(Store().Entry(head))->prev = Null;

            ResetHook(temp);
            return temp;
        }

        T* PopBack()
        {
            if (tail == Null)
                return nullptr;

            T* temp = Store().Entry(tail);
            tail = Hk(temp)->prev;

            if (tail == Null)
                head = Null;
            else
                Hk(Store().Entry(tail))->next = Null;

            ResetHook(temp);
            return temp;
        }

        [[nodiscard]]
        bool InsertAfter(Iterator it, T* value)
        {
            if (it == End())
                return PushBack(value);

            const Index index = IndexOf(value);
            if (index == Null || LinkedAt(index, value))
                return false;

            T* after = Store().Entry(it.index);
            Hk(value)->prev = it.index;
            Hk(value)->next = Hk(after)->next;
            Hk(after)->next = index;

            if (Hk(value)->next == Null)
                tail = index;
            else
                Hk(Store().Entry(Hk(value)->next))->prev = index;

            return true;
        }

        [[nodiscard]]
        bool InsertBefore(Iterator it, T* value)
        {
            if (it == End())
                return PushBack(value);

            const Index index = IndexOf(value);
            if (index == Null || LinkedAt(index, value))
                return false;

            T* before = Store().Entry(it.index);
            Hk(value)->next = it.index;
            Hk(value)->prev = Hk(before)->prev;
            Hk(before)->prev = index;

            if (Hk(value)->prev == Null)
                head = index;
            else
                Hk(Store().Entry(Hk(value)->prev))->next = index;

            return true;
        }

        [[nodiscard]]
        bool InsertSorted(T* value, bool (*LessThan)(T* a, T* b))
        {
            if (IndexOf(value) == Null)
                return false;

            if (Empty() || LessThan(value, Store().Entry(head)))
                return PushFront(value);
            if (LessThan(Store().Entry(tail), value))
                return PushBack(value);

            for (auto it = Begin(); it != End(); ++it)
            {
                if (LessThan(value, &*it))
                    return InsertBefore(it, value);
            }

            return PushBack(value);
        }

        [[nodiscard]]
        bool Remove(T* value)
        {
            const Index index = IndexOf(value);
            if (index == Null)
                return false;

            const Index prev = Hk(value)->prev;
            const Index next = Hk(value)->next;

            if (prev == Null && head != index)
                return false;
            if (next == Null && tail != index)
                return false;

            if (prev == Null)
                head = next;
            else
                Hk(Store().Entry(prev))->next = next;

            if (next == Null)
                tail = prev;
            else
                Hk(Store().Entry(next))->prev = prev;

            ResetHook(value);
            return true;
        }

        Iterator Remove(Iterator it)
        {
            if (it == End())
                return End();

            auto next = it;
            ++next;

            if (!Remove(Store().Entry(it.index)))
                return End();

            return next;
        }

        [[nodiscard]]
        bool Splice(IndexList& other)
        {
            if (&other == this || !Store().SameAs(other.Store()))
                return false;
            if (other.Empty())
                return true;

            if (Empty())
                head = other.head;
            else
            {
                Hk(Store().Entry(tail))->next = other.head;
                Hk(Store().Entry(other.head))->prev = tail;
            }

            tail = other.tail;
            other.head = Null;
            other.tail = Null;

            return true;
        }

        [[nodiscard]]
        bool Exchange(IndexList& other)
        {
            if (!Store().SameAs(other.Store()))
                return false;

            sl::Swap(other.head, head);
            sl::Swap(other.tail, tail);

            return true;
        }
    };
}
