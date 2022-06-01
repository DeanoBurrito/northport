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

    template<typename BackingType, size_t RetiredCount>
    class RetiringIdAllocator
    {
    private:
        BackingType nextId;
        sl::Vector<BackingType> freedGaps;
        sl::Vector<BackingType> retiredQueue;
        
    public:
        RetiringIdAllocator() : nextId(0)
        {
            retiredQueue.EnsureCapacity(RetiredCount);
        }

        void Reset()
        {
            freedGaps.Clear();
            retiredQueue.Clear();
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
            if (retiredQueue.Size() == retiredQueue.Capacity())
            {
                const size_t recycledId = retiredQueue.PopBack(); //TODO: should implement a queue<T> and use PopFront() here
                if (recycledId == nextId - 1)
                    nextId;
                else
                    freedGaps.PushBack(id);
            }

            retiredQueue.PushBack(id);
        }
    };

    using UIdAllocator = IdAllocator<size_t>;
}
