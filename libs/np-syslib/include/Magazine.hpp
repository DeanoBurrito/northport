#pragma once

#include <Types.hpp>
#include <containers/List.hpp>
#include <Compiler.hpp>

namespace sl
{
    template<typename T>
    struct Magazine
    {
        sl::FwdListHook hook;
        size_t count;
        T items[];
    };

    template<typename T, size_t Depth, 
        void DepoExhange(Magazine<T>**, size_t size), 
        void DepoInit(Magazine<T>** full, Magazine<T>** empty, size_t size)>
    class MagazineCache
    {
    using Mag = Magazine<T>;

    private:
        Mag* loaded = nullptr;
        Mag* previous = nullptr;
        size_t size = sizeof(T);

    public:
        void SetSize(size_t size)
        {
            this->size = size;
        }

        T Alloc()
        {
            if (loaded == nullptr)
            {
                DepoInit(&loaded, &previous, size);
                if (loaded == nullptr)
                    return nullptr;
            }

            if (loaded->count == 0)
                sl::Swap(loaded, previous);

            if (loaded->count == 0)
                DepoExhange(&loaded, size);

            if (loaded->count == 0)
                return nullptr;

            return loaded->items[--loaded->count];
        }

        void Free(T ptr)
        {
            if (loaded == nullptr)
            {
                DepoInit(&loaded, &previous, size);
                if (loaded == nullptr)
                    return;
            }

            if (loaded->count == Depth)
                sl::Swap(loaded, previous);
            
            if (loaded->count == Depth)
                DepoExhange(&loaded, size);

            if (loaded->count == Depth)
                SL_UNREACHABLE();

            loaded->items[loaded->count++] = ptr;
        }
    };
}
