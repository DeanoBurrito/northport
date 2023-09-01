#pragma once

#include <stddef.h>

namespace sl
{
    namespace Intrusive
    {
        template<typename T>
        class FwdList
        {
        private:
            T* head;

        public:
            using Iterator = T*;

            constexpr FwdList() : head(nullptr)
            {}

            ~FwdList()
            { head = nullptr; }

            FwdList(const FwdList&) = delete;
            FwdList& operator=(const FwdList&) = delete;
            FwdList(FwdList&&) = delete;
            FwdList& operator=(FwdList&&) = delete;

            T& Front()
            { return *head; }

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
            }

            T* PopFront()
            {
                if (head == nullptr)
                    return nullptr;
                T* temp = head;
                head = head->next;
                return temp;
            }

            void InsertAfter(Iterator it, T* value)
            {
                if (it == End())
                {
                    T* scan = head;
                    while (scan->next != End())
                        scan = scan->next;
                    it = scan;
                }

                value->next = nullptr;
                it->next = value;
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
                return temp;
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