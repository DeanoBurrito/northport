#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sl
{
    constexpr inline bool BitmapGet(uint8_t* bitmap, size_t index)
    {
        const size_t byte = index / 8;
        const size_t bit = index & 0b111;
        return bitmap[byte] & (1 << bit);
    }

    //returns if a change was made (bit was previously cleared)
    constexpr inline bool BitmapSet(uint8_t* bitmap, size_t index)
    {
        const size_t byte = index / 8;
        const size_t bit = index & 0b111;
        const bool prev = (bitmap[byte] & (1 << bit)) != 0;
        bitmap[byte] |= 1 << bit;
        return !prev;
    }

    //returns if a change was made (bit was previously set)
    constexpr inline bool BitmapClear(uint8_t* bitmap, size_t index)
    {
        const size_t byte = index / 8;
        const size_t bit = index & 0b111;
        const bool prev = (bitmap[byte] & (1 << bit)) != 0;
        bitmap[byte] &= ~(1 << bit);
        return prev;
    }
    
    //finds the first clear bit in a bitmap, if any.
    constexpr inline size_t BitmapFindClear(uint8_t* bitmap, size_t limit)
    {
        for (size_t i = 0; i < limit; i++)
        {
            if (bitmap[i / 8] == 0xFF)
            {
                i += 7;
                continue; //byte-wise skip
            }
            if (BitmapGet(bitmap, i))
                continue;
            return i;
        }

        return limit;
    }
}
