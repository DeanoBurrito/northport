#pragma once

#include <stddef.h>
#include <PlacementNew.h>
#include <Memory.h>
#include <CppStd.h>

namespace sl
{
    template<typename T>
    class Vector
    {
    private:
        T* elements;
        size_t size;
        size_t capacity;

        void EnsureCapacity(size_t neededCapacity)
        {
            if (neededCapacity <= capacity)
                return;

            size_t newCapacity = capacity * 2 - (capacity / 2);
            T* newElements = (T*)malloc(sizeof(T) * newCapacity);
            for (size_t i = 0; i < capacity; i++)
                new(&newElements[i]) T(sl::move(elements[i]));
            
            for (size_t i = 0; i < size; i++)
                elements[i].~T();
            free(elements);

            elements = newElements;
            capacity = newCapacity;
        }

    public:
        friend void Swap(Vector& a, Vector& b)
        {
            sl::swap(a.elements, b.elements);
            sl::swap(a.size, b.size);
            sl::swap(a.capacity, b.capacity);
        }

        Vector() : elements(nullptr), size(0), capacity(0)
        {}

        Vector(size_t initialCapacity) : Vector()
        { EnsureCapacity(initialCapacity); }

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
            Swap(*this, other);
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
            return latest;
        }

        T& PushBack(T&& elem)
        {
            EnsureCapacity(size + 1);

            T* latest = new(&elements[size]) T(sl::move(elem));
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
            T elem = sl::move(elements[size]);
            elements[size].~T();
            return elem;
        }

        void Clear()
        {
            for (size_t i = 0; i < size; i++)
                elements[i].~T();
            size = 0;
        }

        void DetachData()
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