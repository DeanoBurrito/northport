#pragma once

#include <containers/Vector.h>

namespace sl
{
    template<typename BackingType>
    class IdAllocator
    {
    private:
        BackingType nextId;
        sl::Vector<BackingType> freedGaps;

    public:
        IdAllocator() : nextId(0)
        {}

        void Reset()
        {
            freedGaps.Clear();
            nextId = 0;
        }

        BackingType Alloc()
        {
            BackingType ret;
            if (freedGaps.Size() > 0)
                ret = freedGaps.PopBack();
            else
            {
                ret = nextId;
                nextId++;
            }
            return ret;
        }

        void Free(BackingType id)
        {
            if (id == nextId - 1)
                nextId--;
            else
                freedGaps.PushBack(id);
        }
    };

    using UIdAllocator = IdAllocator<size_t>;
}
