#pragma once

#include <containers/List.h>
#include <Optional.h>
#include <Allocator.h>
#include <Maths.h>

namespace sl
{
    template<typename IdType, typename SizeType, typename Allocator = DefaultAllocator>
    class RangeAllocator
    {
    private:
        struct Range
        {
            sl::ListHook hook;
            IdType base;
            SizeType length;
        };

        Allocator alloc;
        size_t scaleShift;
        sl::List<Range, &Range::hook> freeRanges;

    public:
        constexpr RangeAllocator() : alloc {}, scaleShift { 1 }, freeRanges() {}

        RangeAllocator(IdType base, SizeType length, size_t idScaleShift)
        {
            scaleShift = idScaleShift;

            Range* range = static_cast<Range*>(alloc.Allocate(sizeof(*range)));
            if (range == nullptr)
                return;
            range = new(range) Range();

            range->base = base >> scaleShift;
            range->length = length >> scaleShift;
            freeRanges.PushFront(range);
        }

        sl::Opt<IdType> Alloc(SizeType length)
        {
            if (freeRanges.Empty() || length == 0)
                return {};

            length = sl::AlignUp(length, 1 << scaleShift) >> scaleShift;

            for (auto it = freeRanges.Begin(); it != freeRanges.End(); ++it)
            {
                if (it->length < length)
                    continue;

                if (it->length == length)
                {
                    freeRanges.Remove(it);
                    const IdType base = it->base;
                    alloc.Deallocate(&*it, sizeof(*it));

                    return base << scaleShift;
                }

                const IdType base = it->base;
                it->base += length;
                it->length -= length;
                return base << scaleShift;
            }
        }

        void Free(IdType base, SizeType length)
        {
            base >>= scaleShift;
            length = sl::AlignUp(length, 1 << scaleShift) >> scaleShift;

            for (auto it = freeRanges.Begin(); it != freeRanges.End(); ++it)
            {
                Range* range = *it;
                if (base == range->base + range->length)
                {
                    range->length += length;
                    return;
                }
                if (base + length == range->base)
                {
                    range->base = base;
                    range->length += length;
                    return;
                }
            }

            Range* range = static_cast<Range*>(alloc.Allocate(sizeof(*range)));
            if (range == nullptr)
                return;
            range = new(range) Range();

            range->base = base;
            range->length = length;
            freeRanges.InsertSorted(range, [](Range* a, Range* b) -> bool 
                { return a->base < b->base; });
        }
    };
}
