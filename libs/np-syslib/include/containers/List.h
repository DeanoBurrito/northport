#pragma once

#include <CppUtils.h>

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
        { return &(value->*Hook); }

        class Iterator
        {
        friend FwdList;
        private:
            T* ptr;

            constexpr Iterator(T* entry) : ptr(entry) {}

        public:
            constexpr bool operator==(const Iterator& other) const
            { return ptr == other.ptr; }

            constexpr bool operator!=(const Iterator& other) const
            { return ptr != other.ptr; }

            constexpr Iterator& operator++()
            {
                ptr = static_cast<T*>(Hk(ptr)->next);
                return *this;
            }

            constexpr T& operator*()
            { return *ptr; }

            constexpr T* operator->()
            { return ptr; }

            constexpr const T& operator*() const
            { return *ptr; }

            constexpr const T* operator->() const
            { return ptr; }
        };

        constexpr FwdList() : head(nullptr), tail(nullptr) {}

        FwdList(const FwdList&) = delete;
        FwdList& operator=(const FwdList&) = delete;
        FwdList(FwdList&&) = delete;
        FwdList& operator=(FwdList&&) = delete;

        T& Front() const
        { return *head; }

        T& Back() const
        { return *tail; }

        Iterator Begin()
        { return head; }

        Iterator End()
        { return nullptr; }

        const Iterator Begin() const
        { return head; }

        const Iterator End() const
        { return nullptr; }

        bool Empty() const
        { return head == nullptr; }

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
                tail = static_cast<T*>(Hk(temp)->next);
            return temp;
        }

        template<typename Comparison>
        void Sort(Comparison comp)
        {
            for (auto i = head; i != nullptr; i = static_cast<T*>(Hk(i)->next))
            {
                for (auto j = Hk(i)->next; j != nullptr; j = Hk(j)->next)
                {
                    if (comp(*i, *j))
                    {
                        sl::Swap(*i, *j);
                        sl::Swap(Hk(i)->next, Hk(j)->next);
                    }
                }
            }
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
                if (LessThan(value, &*it))
                    return InsertAfter(prev, value);
            }
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
        { return &(value->*Hook); }

        class Iterator
        {
        friend List;
        private:
            T* ptr;

            constexpr Iterator(T* entry) : ptr(entry) {}

        public:
            constexpr bool operator==(const Iterator& other) const
            { return ptr == other.ptr; }

            constexpr bool operator!=(const Iterator& other) const
            { return ptr != other.ptr; }

            constexpr Iterator& operator++()
            {
                ptr = static_cast<T*>(Hk(ptr)->next);
                return *this;
            }

            constexpr T& operator*()
            { return *ptr; }

            constexpr T* operator->()
            { return ptr; }

            constexpr const T& operator*() const
            { return *ptr; }

            constexpr const T* operator->() const
            { return ptr; }
        };

        constexpr List() : head(nullptr), tail(nullptr) {}

        List(const List&) = delete;
        List& operator=(const List&) = delete;
        List(List&&) = delete;
        List& operator=(List&&) = delete;

        T& Front()
        { return *head; }

        T& Back()
        { return *tail; }

        Iterator Begin()
        { return head; }

        Iterator End()
        { return nullptr; }

        const Iterator Begin() const
        { return head; }

        const Iterator End() const
        { return nullptr; }

        bool Empty() const
        { return head == nullptr; }

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
            Hk(value)->prev = it.ptr;

            if (it.ptr == nullptr)
            {
                Hk(value)->next = head;
                head = value;
            }
            else
            {
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
            {
                Hk(value)->prev = nullptr;
                Hk(value)->next = head;
                if (head == nullptr)
                    tail = value;
                else
                    Hk(head)->prev = value;
                head = value;
            }
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
        }

        Iterator Remove(T* value)
        {
            T* prev = static_cast<T*>(Hk(value)->prev);
            T* next = static_cast<T*>(Hk(value)->next);

            if (prev == nullptr)
                head = next;
            else
                Hk(prev)->next = next;

            if (next == nullptr)
                tail = prev;
            else
                Hk(next)->prev = prev;

            return next;
        }

        Iterator Remove(Iterator it)
        { return Remove(it.ptr); }
    };
}
