#pragma once

#include <PlacementNew.h>
#include <CppUtils.h>

namespace sl
{
    template<typename T>
    class Lazy
    {
    private:
        alignas(T) uint8_t storage[sizeof(T)];
        bool initialized;
    
    public:
        constexpr Lazy() : initialized(false)
        {}

        template<typename... Args>
        T& Init(Args&&... args)
        {
            if (initialized)
                return *Get();
            
            new (storage) T(sl::Forward<Args>(args)...);
            initialized = true;
            return *Get();
        }

        T* operator->()
        {
            return Get();
        }

        T& operator*()
        {
            return *Get();
        }

        T* Get()
        {
            return reinterpret_cast<T*>(storage);
        }
    };
}
