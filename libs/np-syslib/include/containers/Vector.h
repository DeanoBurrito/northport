/*
---- NOTE ----
This vector implementation is mostly from the frigg library (https://github.com/managarm/frigg.
While I wrote this myself, and it's not completely identical, I had just read the frigg implementation, and so a lot of it ended up being the same.
Lessons learned for the future!

The license (as required) is listed below, and as syslib is licensed under the same license, there should be no issues.
If you're a maintainer of frigg and have issues, please dont hesitate to reach out.

---- LICENSE ----
MIT License
Copyright 2014-2021 Frigg Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#pragma once

#include <stddef.h>
#include <PlacementNew.h>
#include <Memory.h>
#include <Utilities.h>

namespace sl
{
    template<typename T>
    class Vector
    {
    private:
        T* elements = nullptr;
        size_t size = 0;
        size_t capacity = 0;

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
            T* newElements = (T*)malloc(sizeof(T) * newCapacity);
            for (size_t i = 0; i < capacity; i++)
                new(&newElements[i]) T(sl::Move(elements[i]));
            
            for (size_t i = 0; i < size; i++)
                elements[i].~T();
            if (elements != nullptr)
                free(elements);

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
                free(elements);
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
            
            Erase(index, index);
        }

        void Erase(size_t first, size_t last)
        {
            if (first > last || last >= size)
                return;

            const size_t shuffleCount = size - last - 1;
            for (size_t i = 0; i < shuffleCount; i++)
                new(&elements[i + first]) T(sl::Move(elements[i + last + 1]));
            for (size_t i = last + 1; i < size; i++)
                elements[i].~T();

            size -= shuffleCount;
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