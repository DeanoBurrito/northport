#pragma once

#include <stddef.h>
#include <CppUtils.h>

namespace sl
{
    namespace Intrusive
    {
        template<typename T>
        class FwdList
        {
        private:
            T* head;
            T* tail;

        public:
            using Iterator = T*;

            constexpr FwdList() : head(nullptr), tail(nullptr)
            {}

            FwdList(const FwdList&) = delete;
            FwdList& operator=(const FwdList&) = delete;
            FwdList(FwdList&&) = delete;
            FwdList& operator=(FwdList&&) = delete;

            T& Front()
            { return *head; }

            T& Back()
            { return *tail; }

            Iterator Begin()
            { return head; }

            Iterator End()
            { return nullptr; }

            bool Empty()
            { return head == nullptr; }

            void PushFront(T* value)
            {
                value->next = head;
                head = value;
                if (tail == nullptr)
                    tail = head;
            }

            void PushBack(T* value)
            {
                value->next = nullptr;
                if (Empty())
                    head = value;
                else
                    tail->next = value;
                tail = value;
            }

            T* PopFront()
            {
                if (head == nullptr)
                    return nullptr;

                T* temp = head;
                head = head->next;

                if (Empty())
                    tail = nullptr;
                return temp;
            }

            void InsertAfter(Iterator it, T* value)
            {
                if (it == End())
                    return PushBack(value);

                value->next = it->next;
                it->next = value;
                
                if (it == tail)
                    tail = value;
            }

            T* EraseAfter(Iterator it)
            {
                if (it == End())
                    return nullptr;

                T* temp = it->next;
                if (temp != nullptr)
                    it->next = temp->next;
                else
                    it->next = nullptr;
                if (temp == tail)
                    tail = temp->next;
                return temp;
            }

            T* Remove(T* value)
            {
                if (value == nullptr || head == nullptr)
                    return nullptr;

                if (head == value)
                {
                    PopFront();
                    return head;
                }
                    
                T* scan = head;
                while (scan->next != nullptr)
                {
                    if (scan->next == value)
                        return EraseAfter(scan);
                    scan = scan->next;
                }

                return nullptr;
            }

            template<typename Comparison>
            void Sort(Comparison comp)
            {
                for (auto i = head; i != nullptr; i = i->next)
                {
                    for (auto j = i->next; j != nullptr; j = j->next)
                    {
                        if (comp(*i, *j))
                        {
                            sl::Swap(*i, *j);
                            sl::Swap(i->next, j->next);
                        }
                    }
                }
            }
        };

        template<typename T>
        class List
        {
        private:
            //TODO: impl, not copying other list
        };
    }

    template<typename T>
    using IntrFwdList = Intrusive::FwdList<T>;

    template<typename T>
    using IntrList = Intrusive::List<T>;
}
