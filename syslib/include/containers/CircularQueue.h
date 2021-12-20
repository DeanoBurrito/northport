#pragma once

#include <stddef.h>
#include <PlacementNew.h>
#include <Memory.h>
#include <CppStd.h>
#include <Maths.h>

namespace sl
{
    template<typename CircularQueueT, typename T>
    class CircularQueueIterator
    {
    friend CircularQueueT;
    private:
        CircularQueueT* owner;
        size_t index;

        explicit CircularQueueIterator(CircularQueueT owner, size_t index) : index(index), owner(owner)
        {}

    public:
        bool operator!=(const CircularQueueIterator& other)
        { return index != other.index; }

        bool operator==(const CircularQueueIterator& other)
        { return index == other.index; }

        void operator++()
        {
            if (index == owner->capacity)
                return;

            if (index == owner->last)
            {
                index = owner->capacity;
                return;
            }
            
            index++;

            if (index == owner->capacity && owner->first > owner->last)
                index = 0;
        }

        T& operator*()
        { return owner->buffer[index]; }

        T* operator->()
        { return &owner->buffer[index]; }
    };

    template<typename T>
    class CircularQueue
    {
    private:
        T* buffer;
        size_t first;
        size_t last;
        size_t capacity;
        size_t count;

        T* DetachBuffer()
        {
            T* buff = buffer;
            buffer = new T[0];
            capacity = 0;
            return buff;
        }

    public:
        CircularQueue(size_t bufferSize)
        {
            count = 0;
            capacity = bufferSize;
            first = last = capacity;
            buffer = new T[bufferSize];
        }

        ~CircularQueue()
        {
            if (buffer)
                delete[] buffer;
        }

        CircularQueue(const CircularQueue& other)
        {
            capacity = other.capacity;
            count = other.count;
            first = other.first;
            last = other.last;

            buffer = new T[capacity];
            sl::ComplexCopy(other.buffer, 0, buffer, 0, capacity);
        }

        CircularQueue& operator=(const CircularQueue& other)
        {
            if (buffer)
                buffer = delete[] buffer;
            
            capacity = other.capacity;
            count = other.count;
            first = other.first;
            last = other.last;

            buffer = new T[capacity];
            sl::ComplexCopy(other.buffer, 0, buffer, 0, capacity);

            return *this;
        }

        CircularQueue(CircularQueue&& from)
        {
            capacity = from.capacity;
            count = from.count;
            first = from.first;
            last = from.last;
            buffer = from.DetachBuffer();
        }

        CircularQueue& operator=(CircularQueue&& from)
        {
            if (buffer)
                delete[] buffer;
            
            capacity = from.capacity;
            count = from.count;
            first = from.first;
            last = from.last;
            buffer = from.DetachBuffer();

            return *this;
        }

        T& PushBack(const T& element)
        {
            if (first == capacity)
            {
                first = last = 0;
            }
            else
            {
                last++;
                if (last == capacity)
                    last = 0;
                if (last == first)
                    first++;
                if (first == capacity)
                    first = 0;
            }

            count++;
            if (count > capacity)
                count = capacity;

            T* latest = new (&buffer[last]) T(element);
            return *latest;
        }

        T& PushBack(T&& element)
        {
            if (first == capacity)
            {
                first = last = 0;
            }
            else
            {
                last++;
                if (last == capacity)
                    last = 0;
                if (last == first)
                    first++;
                if (first == capacity)
                    first = 0;
            }

            count++;
            if (count > capacity)
                count = capacity;
            
            T* latest = new (&buffer[last]) T(move(element));
            return *latest;
        }

        template<typename... Args>
        T& EmplaceBack(Args... args)
        {
            if (first == capacity)
            {
                first = last = 0;
            }
            else
            {
                last++;
                if (last == capacity)
                    last = 0;
                if (last == first)
                    first++;
                if (first == capacity)
                    first = 0;
            }

            count++;
            if (count > capacity)
                count = capacity;
            
            T* latest = new (&buffer[last]) T(Forward<Args>(args)...);
            return *latest;
        }

        T PopFront()
        {
            count--;
        }

        size_t PopInto(T* into, size_t number)
        {
            if (!buffer)
                return 0;
            if (first == capacity)
                return 0;
            
            const size_t copyCount = sl::min(number, count);
            if (first < last)
            {   //easy single copy
                sl::ComplexCopy(buffer, first, into, 0, copyCount);
            }
            else
            {   //annoying double copy
                sl::ComplexCopy(buffer, first, into, 0, capacity - first);
                sl::ComplexCopy(buffer, 0, into, first, last);
            }

            count -= copyCount;
            return copyCount;
        }

        T& First()
        { return buffer[first]; }

        const T& First() const
        { return const_cast<const T&>(buffer[first]); }

        T& Last()
        { return buffer[last]; }

        const T& Last() const
        { return const_cast<const T&>(buffer[last]); }

        using Iterator = CircularQueueIterator<CircularQueue, T>;
        friend Iterator;
        using ConstIterator = CircularQueueIterator<const CircularQueue, const T>;
        friend ConstIterator;

        Iterator Begin()
        { return Iterator(first); }

        Iterator End()
        { return Iterator(capacity); }

        ConstIterator Begin() const
        { return ConstIterator(first); }

        ConstIterator End() const
        { return ConstIterator(capacity); }

        size_t Size() const
        { return count; }

        size_t Capacity() const
        { return capacity; }
    };
}
