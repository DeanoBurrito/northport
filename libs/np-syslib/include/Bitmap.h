#pragma once

#include <stdint.h>
#include <stddef.h>

namespace sl
{
    class Bitmap
    {
    private:
        uint8_t* buffer;
        size_t entryCount;

    public:
        Bitmap() : buffer(nullptr), entryCount(0) {}
        Bitmap(size_t initialEntries);

        ~Bitmap();

        void Resize(size_t count);
        void Reset(bool defaultToCleared = true);
        size_t Size() const;

        //returns whether any state was changed, false if bit was already set.
        bool Set(size_t index);
        bool Set(size_t index, bool state);
        //returns if bit was cleared, false if bit was already clear.
        bool Clear(size_t index);
        bool Get(size_t index);

        //returns the index of the first cleared/set bit
        size_t FindFirst(bool cleared = true);
        //returns the first index, and flips the bit.
        size_t FindAndClaimFirst(bool cleared = true);
    };
}
