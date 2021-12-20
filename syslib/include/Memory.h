#pragma once

#include <NativePtr.h>
#include <PlacementNew.h>

//prototypes - these are either implemented by kernel or userspace dl
void* malloc(size_t);
void free(void*);

namespace sl
{
    template<typename F, typename E>
    [[gnu::always_inline]]
    inline bool EnumHasFlag(F enumeration, E entry)
    {
        return ((size_t)enumeration & (size_t)entry) != 0;
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumSetFlag(F enumeration, E entry)
    {
        return (F)((size_t)enumeration | (size_t)entry);
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumClearFlag(F enumeration, E entry)
    {
        return (F)((size_t)enumeration & ~(size_t)entry);
    }

    template<typename F, typename E>
    [[gnu::always_inline]]
    inline F EnumSetFlagState(F enumeration, E entry, bool set)
    {
        if (set)
            return EnumSetFlag(enumeration, entry);
        else
            return EnumClearFlag(enumeration, entry);
    }
    
    template<typename T>
    T&& move(T&& t)
    { return static_cast<T&&>(t); }

    template<typename T>
    void swap(T& a, T& b)
    {
        T temp = move(a);
        a = move(b);
        b = move(temp);
    }
    
    template <typename WordType>
    [[gnu::always_inline]]
    inline void MemWrite(sl::NativePtr where, WordType val)
    {
        *reinterpret_cast<volatile WordType*>(where.ptr) = val;
    }

    template <typename WordType>
    [[gnu::always_inline]]
    inline WordType MemRead(sl::NativePtr where)
    {
        return *reinterpret_cast<volatile WordType*>(where.ptr);
    }

    [[gnu::always_inline]]
    inline void* StackAlloc(size_t size)
    {
        return __builtin_alloca(size);
    }

    template<typename WordType = NativeUInt, bool inverse = false>
    [[gnu::always_inline]] 
    inline void StackPush(sl::NativePtr& sp, WordType value)
    {
        if (!inverse)
            sp.raw -= sizeof(WordType);
        *sp.As<WordType>() = value;
        if (inverse)
            sp.raw += sizeof(WordType);
    }

    template<typename WordType = NativeUInt, bool inverse = false>
    [[gnu::always_inline]]
    inline WordType StackPop(sl::NativePtr& sp)
    {
        if (inverse)
            sp.raw -= sizeof(WordType);
        WordType value = *sp.As<WordType>();
        if (!inverse)
            sp.raw += sizeof(WordType);
        return value;
    }

    template<typename T>
    [[gnu::always_inline]]
    inline void ComplexCopy(T* src, size_t srcOffset, T* dest, size_t destOffset, size_t count)
    {
        for (size_t i = 0; i < count; i++)
        {
            new (&dest[i + destOffset]) T(src[i + srcOffset]);
        }
    }

    template<typename T>
    void memsetT(void* const start, T value, size_t valueCount)
    {
        T* const si = reinterpret_cast<T* const>(start);
        
        for (size_t i = 0; i < valueCount; i++)
            si[i] = value;
    }

    void memset(void* const start, uint8_t val, size_t count);

    void memcopy(const void* const source, void* const destination, size_t count);
    void memcopy(const void* const source, size_t sourceOffset, void* const destination, size_t destOffset, size_t count);

    int memcmp(const void* const a, const void* const b, size_t count);

    size_t memfirst(const void* const buff, uint8_t target, size_t upperLimit);
    size_t memfirst(const void* const buff, size_t offset, uint8_t target, size_t upperLimit);
}
