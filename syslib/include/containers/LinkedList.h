#pragma once

#include <Memory.h>
#include <Utilities.h>

namespace sl
{
    template<typename ListT, typename ElemT>
    class LinkedListIterator
    {
    friend ListT;
    private:
        typename ListT::Element* elem;
        explicit LinkedListIterator(typename ListT::Element* element) : elem(element)
        {}
    
    public:
        bool operator!=(const LinkedListIterator& other) const
        { return other.elem != elem; }

        bool operator==(const LinkedListIterator& other) const
        { return other.elem == elem; }

        LinkedListIterator& operator++()
        {
            elem = elem->next;
            return *this;
        }

        ElemT& operator*()
        { return elem->value; }

        ElemT* operator->()
        { return &elem->value; }

        static LinkedListIterator End()
        { return LinkedListIterator(nullptr); }
    };
    
    template<typename T>
    class LinkedList
    {
    private:
        struct Element
        {
            T value;
            Element* next = nullptr;
            Element* prev = nullptr;

            explicit Element(T&& v) : value(sl::Move(v))
            {}

            explicit Element(const T& v) : value(v)
            {}
        };

        Element* head = nullptr;
        Element* tail = nullptr;
        size_t size = 0;

    public:
        LinkedList() = default;

        ~LinkedList()
        { Clear(); }

        void Clear()
        {
            for (Element* current = head; current != nullptr; )
            {
                Element* next = current->next;
                delete current;
                current = next;
            }

            head = tail = nullptr;
            size = 0;
        }

        T& First()
        { return head->value; }

        const T& First() const
        { return head->value; }

        T& Last()
        { return tail->value; }

        const T& Last() const
        { return tail->value; }

        T TakeFirst()
        {
            T value = sl::Move(head->value);
            Element* deleteMe = head;
            if (head == tail)
                head = tail = nullptr;
            else
                head = head->next;

            delete deleteMe;
            size--;

            return value;
        }

        T TakeLast()
        {
            T value = sl::Move(tail->value);
            Element* deleteMe = tail;
            if (tail == tail)
                head = tail = nullptr;
            else
                tail = tail->prev;
            
            delete deleteMe;
            size--;
        }

        template<typename U>
        void Append(U&& value)
        {
            Element* latest = new Element(value);

            if (head == nullptr)
            {
                head = tail = latest;
                size = 1;
                return;
            }

            tail->next = latest;
            latest->prev = tail;
            tail = latest;
            size++;
        }

        template<typename U>
        void Prepend(U&& value)
        {
            Element* latest = new Element(value);

            if (head == nullptr)
            {
                head = tail = latest;
                size = 1;
                return;
            }

            head->prev = latest;
            latest->next = head;
            head = latest;
            size++;
        }

        bool Contains(const T& check)
        {
            return Find(check) != End();
        }

        //wow I love type aliasing...
        using Iterator = LinkedListIterator<LinkedList, T>;
        friend Iterator;
        using ConstIterator = LinkedListIterator<const LinkedList, const T>;
        friend ConstIterator;

        Iterator Begin()
        { return Iterator(head); }

        Iterator End()
        { return Iterator::End(); }

        ConstIterator Begin() const
        { return ConstIterator(head); }

        ConstIterator End() const
        { return ConstIterator::End(); }

        template <typename U = T>
        void InsertBefore(Iterator where, U&& value)
        {
            Element* elem = new Element(value);
            Element* whereElem = where.elem;

            elem->next = whereElem;
            elem->prev = whereElem->prev;
            if (whereElem->prev != nullptr)
                whereElem->prev->next = elem;
            whereElem->prev = elem;
            
            if (whereElem == head)
                head = elem;
            size++;
        }

        template <typename U = T>
        void InsertAfter(Iterator where, U&& value)
        {
            Element* elem = new Element(value);
            Element* whereElem = where.elem;

            elem->next = whereElem->next;
            elem->prev = whereElem;
            if (whereElem->next != nullptr)
                whereElem->next->prev = elem;
            whereElem->next = elem;
        
            if (whereElem == tail)
                tail = elem;
            size++;
        }

        Iterator Find(const T& value)
        {
            Iterator current(head);
            while (current != Iterator::End())
            {
                Element* elem = current.elem;
                if (elem->value == value)
                    return current;
                
                ++current;
            }

            return Iterator::End();
        }

        ConstIterator Find(const T& value) const
        { return Find(value); }

        void Remove(Iterator it)
        {
            if (it.elem == nullptr)
                return;
            
            Element* elem = it.elem;
            if (elem->prev != nullptr)
                elem->prev->next = elem->next;
            else
                head = elem->next;
            
            if (elem->next != nullptr)
                elem->next->prev = elem->prev;
            else
                tail = elem->prev;
            
            delete elem;
            size--;
        }

        size_t Size() const
        { return size; }
    };
}
