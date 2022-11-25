#pragma once
/*
This vector implementation is based on frigg's implementation (https://github.com/managarm/frigg).
As required, the license text is below.

MIT License
Copyright 2014-2021 Frigg Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <Memory.h>
#include <CppUtils.h>
#include <Allocator.h>

namespace sl
{
    template<typename T, typename Allocator = DefaultAllocator>
    class Vector
    {
    private:
        T* elements = nullptr;
        size_t size = 0;
        size_t capacity = 0;
        Allocator alloc;

    public:
        friend void Swap(Vector& a, Vector& b)
        {
            sl::Swap(a.elements, b.elements);
            sl::Swap(a.size, b.size);
            sl::Swap(a.capacity, b.capacity);
        }

        void EnsureCapacity(size_t neededCapacity)
        {
            if (neededCapacity <= capacity)
                return;

            size_t newCapacity = capacity * 2;
            if (newCapacity == 0)
                newCapacity = neededCapacity;
            T* newElements = (T*)alloc.Allocate(sizeof(T) * newCapacity);
            for (size_t i = 0; i < capacity; i++)
                new(&newElements[i]) T(sl::Move(elements[i]));
            
            for (size_t i = 0; i < size; i++)
                elements[i].~T();
            if (elements != nullptr)
                alloc.Deallocate(elements, sizeof(T) * capacity);

            elements = newElements;
            capacity = newCapacity;
        }

        Vector() : elements(nullptr), size(0), capacity(0)
        {}

        Vector(size_t initialCapacity) : Vector()
        { EnsureCapacity(initialCapacity); }

        Vector(T* takingBuffer, size_t bufferLength)
        {
            elements = takingBuffer;
            capacity = bufferLength;
            size = bufferLength;
        }

        ~Vector()
        {
            Clear();
            if (elements)
                alloc.Deallocate(elements, sizeof(T) * capacity);
        }

        Vector(const Vector& other)
        {
            EnsureCapacity(other.size);
            for (size_t i = 0; i < other.size; i++)
                new(&elements[i]) T(other[i]);
            size = other.size;
        }

        Vector& operator=(const Vector& other)
        {
            Clear();
            EnsureCapacity(other.size);
            for (size_t i = 0; i < other.size; i++)
                new(&elements[i]) T(other[i]);
            size = other.size;
            return *this;
        }

        Vector(Vector&& from)
        {
            Swap(*this, from);
        }

        Vector& operator=(Vector&& from)
        {
            Swap(*this, from);
            return *this;
        }

        T& PushBack(const T& elem)
        {
            EnsureCapacity(size + 1);

            T* latest = new(&elements[size]) T(elem);
            size++;
            return *latest;
        }

        T& PushBack(T&& elem)
        {
            EnsureCapacity(size + 1);

            T* latest = new(&elements[size]) T(sl::Move(elem));
            size++;
            return *latest;
        }

        template<typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            EnsureCapacity(size + 1);

            T* latest = new(&elements[size]) T(sl::Forward<Args>(args)...);
            size++;
            return *latest;
        }

        template<typename... Args>
        T& EmplaceAt(size_t index, Args&&... args)
        {
            EnsureCapacity(index + 1);
            while (size < index)
            {
                new(&elements[size]) T();
                size++;
            }

            T* latest = new(&elements[index]) T(sl::Forward<Args>(args)...);
            if (index >= size)
                size++;
            return *latest;
        }

        T PopBack()
        {
            size--;
            T elem = sl::Move(elements[size]);
            elements[size].~T();
            return elem;
        }

        void Clear()
        {
            for (size_t i = 0; i < size; i++)
                elements[i].~T();
            size = 0;
        }

        void Erase(size_t index)
        {
            if (index >= size)
                return;
            
            Erase(index, 1);
        }

        void Erase(size_t index, size_t count)
        {
            if (count == 0 || index >= size)
                return;
            
            if (count > index)
                count -= index;
            for (size_t i = index; i < index + count; i++)
                At(i).~T();
            for (size_t i = index + count; i < size; i++)
            {
                new (&elements[i - count]) T(sl::Move(At(i)));
                At(i).~T();
            }
            
            size -= count;
        }

        template<typename... Args>
        T& Emplace(size_t index, Args&&... args)
        {
            if (index >= size)
                return EmplaceAt(index, sl::Forward<Args>(args)...);
            
            EnsureCapacity(size + 1);
            for (size_t i = size; i > index; i--)
                new (&elements[i]) T(sl::Move(elements[i - 1]));
            
            elements[index].~T();
            new (&elements[index]) T(sl::Forward<Args>(args)...);
            size++;

            return elements[index];
        }

        T& Insert(size_t index, const T& elem)
        {
            return Emplace(index, elem);
        }

        [[nodiscard]]
        T* DetachData()
        {
            size = capacity = 0;
            elements = nullptr;
        }

        T* Data()
        { return elements; }

        const T* Data() const
        { return elements; }

        size_t Size() const
        { return size; }

        size_t Capacity() const
        { return capacity; }

        bool Empty() const
        { return size == 0; }

        T* Begin()
        { return elements; }

        const T* Begin() const
        { return elements; }

        T* End()
        { return elements + size; }

        const T* End() const
        { return elements + size; }

        T& Front()
        { return elements[0]; }

        const T& Front() const
        { return elements[0]; }

        T& Back()
        { return elements[size - 1]; }

        const T& Back() const
        { return elements[size - 1]; }

        T& At(size_t index)
        { 
            if (index > size)
                index = size;
            return elements[index];
        }

        const T& At(size_t index) const
        { 
            if (index > size)
                index = size;
            return elements[index];
        }

        T& operator[](size_t index)
        { return At(index); }

        const T& operator[](size_t index) const
        { return At(index); }
    };
}