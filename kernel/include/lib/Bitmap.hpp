#pragma once

#include "Maths.hpp"
#include "Compiler.hpp"

namespace sl
{
    constexpr size_t BitmapWordBits = sizeof(uintptr_t) * 8;

    static_assert(sizeof(uintptr_t) == sizeof(unsigned long),
        "Bitmap scan helpers assume uintptr_t matches unsigned long");

    constexpr inline size_t BitmapWordCount(size_t bits)
    {
        return (bits + BitmapWordBits - 1) / BitmapWordBits;
    }

    constexpr inline size_t BitmapWordFor(size_t index)
    {
        return index / BitmapWordBits;
    }

    constexpr inline uintptr_t BitmapMaskFor(size_t index)
    {
        return static_cast<uintptr_t>(1) << (index % BitmapWordBits);
    }

    constexpr inline bool BitmapTest(const uintptr_t* words, size_t index)
    {
        return words[BitmapWordFor(index)] & BitmapMaskFor(index);
    }

    constexpr inline bool BitmapSet(uintptr_t* words, size_t index)
    {
        const uintptr_t mask = BitmapMaskFor(index);
        auto& word = words[BitmapWordFor(index)];

        const bool prev = word & mask;
        word |= mask;

        return prev;
    }

    constexpr inline bool BitmapClear(uintptr_t* words, size_t index)
    {
        const uintptr_t mask = BitmapMaskFor(index);
        auto& word = words[BitmapWordFor(index)];

        const bool prev = word & mask;
        word &= ~mask;

        return prev;
    }

    constexpr inline bool BitmapFlip(uintptr_t* words, size_t index)
    {
        const uintptr_t mask = BitmapMaskFor(index);
        auto& word = words[BitmapWordFor(index)];

        const bool prev = word & mask;
        word ^= mask;

        return prev;
    }

    inline size_t BitmapFindSet(const uintptr_t* words, size_t bitCount,
        size_t from = 0)
    {
        for (size_t i = from; i < bitCount;)
        {
            const size_t offset = i % BitmapWordBits;
            const uintptr_t word = words[BitmapWordFor(i)] >> offset;

            if (word == 0)
            {
                i += BitmapWordBits - offset;
                continue;
            }

            const size_t found = i + SL_TRAILING_ZEROS(word);

            return found < bitCount ? found : bitCount;
        }

        return bitCount;
    }

    inline size_t BitmapFindClear(const uintptr_t* words, size_t bitCount,
        size_t from = 0)
    {
        for (size_t i = from; i < bitCount;)
        {
            const size_t offset = i % BitmapWordBits;
            const uintptr_t word = ~words[BitmapWordFor(i)] >> offset;

            if (word == 0)
            {
                i += BitmapWordBits - offset;
                continue;
            }

            const size_t found = i + SL_TRAILING_ZEROS(word);

            return found < bitCount ? found : bitCount;
        }

        return bitCount;
    }

    template<size_t InlineWords, typename Alloc>
    class InlineBitmap
    {
        static_assert(InlineWords >= 1,
            "InlineBitmap requires at least one inline word");

    private:
        size_t words;
        union
        {
            uintptr_t inlineStore[InlineWords];
            uintptr_t* heapStore;
        };

        uintptr_t* Store()
        {
            return words > InlineWords ? heapStore : inlineStore;
        }

        const uintptr_t* Store() const
        {
            return words > InlineWords ? heapStore : inlineStore;
        }

    public:
        bool Reset(size_t bitCount)
        {
            if (words > InlineWords)
                Alloc::Free(heapStore, words * sizeof(uintptr_t));

            words = BitmapWordCount(bitCount);

            if (words <= InlineWords)
            {
                for (size_t i = 0; i < InlineWords; i++)
                    inlineStore[i] = 0;

                return true;
            }

            void* ptr = Alloc::Allocate(words * sizeof(uintptr_t));
            if (ptr == nullptr)
            {
                words = 0;

                return false;
            }
            heapStore = static_cast<uintptr_t*>(ptr);

            return true;
        }

        void Destroy()
        {
            if (words > InlineWords)
                Alloc::Free(heapStore, words * sizeof(uintptr_t));

            words = 0;
        }

        bool Set(size_t index)
        {
            return BitmapSet(Store(), index);
        }

        bool Clear(size_t index)
        {
            return BitmapClear(Store(), index);
        }

        bool Has(size_t index) const
        {
            return BitmapTest(Store(), index);
        }

        bool Flip(size_t index)
        {
            return BitmapFlip(Store(), index);
        }

        size_t Size() const
        {
            return words * BitmapWordBits;
        }

        size_t FindSet(size_t from = 0) const
        {
            return BitmapFindSet(Store(), Size(), from);
        }

        size_t FindClear(size_t from = 0) const
        {
            return BitmapFindClear(Store(), Size(), from);
        }
    };
}
