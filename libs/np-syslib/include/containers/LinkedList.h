#pragma once
/*
    Linked list based off of Luna's implementation: https://github.com/thomtl/Luna
    License text is as follows:

    Copyright (c) 2020-2022 Thomas Woertman
    All rights reserved.

    Redistribution and use in source and binary forms, with or without modification, 
    are permitted (subject to the limitations in the disclaimer below) provided that 
    the following conditions are met:

        * Redistributions of source code must retain the above copyright notice, 
        this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright notice, 
        this list of conditions and the following disclaimer in the documentation and/or 
        other materials provided with the distribution.
        * Neither the name of the copyright holder nor the names of its contributors 
        may be used to endorse or promote products derived from this software without 
        pecific prior written permission
        * The software or derivatives of the software may not be used for commercial 
        purposes without specific prior written permission.

    NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE. 
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
    OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
*/

#include <stddef.h>
#include <CppUtils.h>
#include <Allocator.h>

namespace sl
{
    namespace Impl
    {
        template<typename T>
        struct ListNode
        {
            ListNode* next;
            ListNode* prev;

            T& Item()
            { return *static_cast<T*>(this); }

            T& Item() const
            { return static_cast<const T*>(this); }
        };

        template<typename T>
        struct LinkedList
        {
        private:
            using NodeT = ListNode<T>;

            NodeT* head;
            NodeT* tail;
            size_t length;

        public:
            constexpr LinkedList() : head(nullptr), tail(nullptr), length(0)
            {}

            LinkedList(const LinkedList&) = delete;
            LinkedList& operator=(const LinkedList&) = delete;
            LinkedList(LinkedList&&) = delete;
            LinkedList& operator=(LinkedList&&) = delete;

            struct Iterator
            {
            public:
                NodeT* entry;

                Iterator(NodeT* entry) : entry(entry)
                {}

                T& operator*()
                { return entry->item; }

                T* operator->() const
                { return &entry->item; }

                void operator++()
                {
                    if (entry)
                        entry = entry->next;
                }
                
                bool operator==(const Iterator& other) const
                { return entry == other.entry; }

                bool operator!=(const Iterator& other) const
                { return entry != other.entry; }
            };

            Iterator Insert(Iterator where, NodeT* element)
            {
                if (where == Begin())
                {
                    PushFront(element);
                    return Iterator(head);
                }
                else if (where == End())
                {
                    PushBack(element);
                    return Iterator(tail);
                }

                NodeT* prev = where.entry->prev;
                where.entry->prev->next = element;
                where.entry->prev = element;
                element->prev = prev;
                element->next = where.entry;
                length++;

                return Iterator(element);
            }

            void PushFront(NodeT* element)
            {
                element->next = head;
                element->prev = nullptr;

                if (head != nullptr)
                    head->prev = head;
                if (tail == nullptr)
                    tail = element;
                head = element;
                length++;
            }

            void PushBack(NodeT* element)
            {
                element->next = nullptr;
                element->prev = tail;

                if (tail != nullptr)
                    tail->next = element;
                if (head == nullptr)
                    head = element;
                
                tail = element;
                length++;
            }

            [[nodiscard]]
            NodeT* PopFront()
            {
                NodeT* element = head;
                if (head->next != nullptr)
                {
                    head->next->prev = nullptr;
                    head = head->next;
                }
                else
                    head = tail = nullptr;

                length--;
                return element;
            }

            [[nodiscard]]
            NodeT* PopBack()
            {
                NodeT* element = tail;
                if (tail->prev != nullptr)
                {
                    tail->prev->next = nullptr;
                    tail = tail->prev;
                }
                else
                    head = tail = nullptr;

                length--;
                return element;
            }

            [[nodiscard]]
            NodeT* Erase(NodeT* element)
            {
                if (element == head)
                    return PopFront();
                else if (element == tail)
                    return PopBack();

                element->prev->next = element->next;
                element->next->prev = element->prev;
                element->prev = element->next = nullptr;
                length--;

                return element;
            }

            Iterator Begin()
            { return Iterator(head); }
            
            Iterator Begin() const
            { return Iterator(head); }

            Iterator End()
            { return Iterator(nullptr); }

            Iterator End() const
            { return Iterator(nullptr); }

            T& Front()
            { return head->Item(); }

            T& Front() const
            { return head->Item(); }

            T& Back()
            { return tail->Item(); }

            T& Back() const
            { return tail->Item(); }

            size_t Size() const
            { return length; }
        };
    }

    template<typename T, typename Allocator = DefaultAllocator>
    class LinkedList
    {
    private:
        struct Item : public Impl::ListNode<Item>
        {
            T entry;

            template<typename... Args>
            Item(Args&&... args): entry {sl::Forward<Args>(args)...}
            {}

            Item(const T& entry) : entry{entry}
            {}
        };

        Impl::LinkedList<Item> list;
        Allocator alloc;

    public:
        constexpr LinkedList() : list{}
        {}

        ~LinkedList()
        {
            for (auto it = list.Begin(); it != list.End();)
            {
                auto* elem = &it.entry->Item();
                ++it;
                delete elem;
            }
        }

        LinkedList(const LinkedList&) = delete;
        LinkedList& operator=(const LinkedList&) = delete;
        LinkedList(LinkedList&&) = delete;
        LinkedList& operator=(LinkedList&&) = delete;

        struct Iterator
        {
        friend LinkedList;
        private:
            typename Impl::LinkedList<Item>::Iterator it;

        public:
            Iterator(typename Impl::LinkedList<Item>::Iterator it) : it(it)
            {}

            T& operator*()
            { return it.entry->Item().entry; }

            T* operator->() const
            { return &it.entry->Item().entry;}

            void operator++()
            { ++it; }

            bool operator==(const Iterator& other) const
            { return it == other.it; }

            bool operator!=(const Iterator& other) const
            { return it != other.it; }
        };

        Iterator Insert(Iterator where, const T& value)
        {
            auto* entry = new(alloc.Allocate(sizeof(Item))) Item(value);
            return { list.Insert(where.it, entry) };
        }

        void PushFront(const T& value)
        {
            auto* entry = new(alloc.Allocate(sizeof(Item))) Item(value);
            list.PushFront(entry);
        }

        void PushBack(const T& value)
        {
            auto* entry = new(alloc.Allocate(sizeof(Item))) Item(value);
            list.PushBack(entry);
        }

        T PopFront()
        {
            auto* entry = &list.PopFront()->Item();
            T value = entry->entry;
            alloc.Deallocate(entry, sizeof(T));

            return value;
        }

        T PopBack()
        {
            auto* entry = &list.PopBack()->Item();
            T value = entry->entry;
            alloc.Deallocate(entry, sizeof(T));

            return value;
        }

        T Erase(Iterator where)
        {
            auto* item = &where.it.entry->Item();
            (void)list.Erase(item);
            T value = item->entry;
            alloc.Deallocate(item, sizeof(T));

            return value;
        }

        template<typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            auto* entry = new(alloc.Allocate(sizeof(Item))) Item(sl::Forward<Args>(args)...);
            list.PushBack(entry);
            return entry->entry;
        }

        Iterator Begin()
        { return { list.Begin() }; }

        Iterator Begin() const
        { return { list.Begin() }; }

        Iterator End()
        { return { list.End() }; }

        Iterator End() const
        { return { list.End() }; }

        T& Front()
        { return list.Front().entry; }

        T& Front() const
        { return list.Front().entry; }

        T& Back()
        { return list.Back().entry; }

        T& Back() const
        { return list.Back().entry; }

        size_t Size() const
        { return list.Size(); }
    };
}
