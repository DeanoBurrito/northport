#include <Memory.h>
#include <Compiler.h>

/* The memcpy() and memset() templates were originally copied from the managarm
 * microkernel (see https://github.com/managarm/managarm), which also happens to
 * be MIT licensed. The original license is listed below, but I also just want
 * to say thanks to the managarm authors (if any of them ever read this haha).
 *
 * ----------------------------------------------------------------------------
 * Copyright 2014-2020 Managarm Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
namespace sl
{
    template<typename T>
    struct WordHelper 
    {
        enum class [[gnu::may_alias, gnu::aligned(1)]] WordEnum : T {};
    };

    template<typename T>
    using Word = typename WordHelper<T>::WordEnum;

    template<typename T>
    SL_ALWAYS_INLINE
    Word<T> AliasLoad(const unsigned char*& p)
    {
        Word<T> value = *reinterpret_cast<const Word<T>*>(p);
        p += sizeof(T);
        return value;
    }

    template<typename T>
    SL_ALWAYS_INLINE
    void AliasStore(unsigned char*& p, Word<T> value)
    {
        *reinterpret_cast<Word<T>*>(p) = value;
        p += sizeof(T);
    }

    template<typename Word>
    void* MemCopy(void* dest, const void* src, size_t len)
    {
        auto curDest = reinterpret_cast<unsigned char*>(dest);
        auto curSrc = reinterpret_cast<const unsigned char*>(src);

        while (len >= 8 * sizeof(Word))
        {
            auto w0 = AliasLoad<Word>(curSrc);
            auto w1 = AliasLoad<Word>(curSrc);
            auto w2 = AliasLoad<Word>(curSrc);
            auto w3 = AliasLoad<Word>(curSrc);
            auto w4 = AliasLoad<Word>(curSrc);
            auto w5 = AliasLoad<Word>(curSrc);
            auto w6 = AliasLoad<Word>(curSrc);
            auto w7 = AliasLoad<Word>(curSrc);
            AliasStore<Word>(curDest, w0);
            AliasStore<Word>(curDest, w1);
            AliasStore<Word>(curDest, w2);
            AliasStore<Word>(curDest, w3);
            AliasStore<Word>(curDest, w4);
            AliasStore<Word>(curDest, w5);
            AliasStore<Word>(curDest, w6);
            AliasStore<Word>(curDest, w7);

            len -= 8 * sizeof(Word);
        }
        
        if (len >= 4 * sizeof(Word))
        {
            auto w0 = AliasLoad<Word>(curSrc);
            auto w1 = AliasLoad<Word>(curSrc);
            auto w2 = AliasLoad<Word>(curSrc);
            auto w3 = AliasLoad<Word>(curSrc);
            AliasStore<Word>(curDest, w0);
            AliasStore<Word>(curDest, w1);
            AliasStore<Word>(curDest, w2);
            AliasStore<Word>(curDest, w3);

            len -= 4 * sizeof(Word);
        }

        if (len >= 2 * sizeof(Word))
        {
            auto w0 = AliasLoad<Word>(curSrc);
            auto w1 = AliasLoad<Word>(curSrc);
            AliasStore<Word>(curDest, w0);
            AliasStore<Word>(curDest, w1);

            len -= 2 * sizeof(Word);
        }

        if (len >= sizeof(Word))
        {
            auto w0 = AliasLoad<Word>(curSrc);
            AliasStore<Word>(curDest, w0);

            len -= sizeof(Word);
        }

        while (len != 0)
        {
            *curDest++ = *curSrc++;
            len--;
        }

        return dest;
    }

    template<typename T>
    void* MemSet(void* dest, int value, size_t len)
    {
        T fill = value;
        for (size_t i = 0; i < sizeof(T); i++)
            fill = (fill << 8) | value;
        Word<T> pattern = static_cast<Word<T>>(fill);

        auto curDest = reinterpret_cast<unsigned char*>(dest);

        constexpr uintptr_t Mask = (sizeof(T) << 8) - 1;
        while ((len & (reinterpret_cast<uintptr_t>(curDest) & Mask)) && len > 0)
        {
            *curDest++ = value;
            --len;
        }

        while (len >= 8 * sizeof(T))
        {
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);

            len -= 8 * sizeof(T);
        }

        if (len >= 4 * sizeof(T))
        {
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);

            len -= 4 * sizeof(T);
        }

        if (len >= 2 * sizeof(T))
        {
            AliasStore<T>(curDest, pattern);
            AliasStore<T>(curDest, pattern);

            len -= 2 * sizeof(T);
        }

        if (len >= sizeof(T))
        {
            AliasStore<T>(curDest, pattern);

            len -= sizeof(T);
        }

        while (len != 0)
        {
            *curDest++ = value;
            len--;
        }

        return dest;
    }

    void* MemCopy(void* dest, const void* src, size_t len)
    {
#if defined(__LP64__)
        return MemCopy<uint64_t>(dest, src, len);
#elif defined(__ILP32__)
        return MemCopy<uint32_t>(dest, src, len);
#else
    #error "wtf are you compiling for"
#endif
    }

    void* MemSet(void* dest, int value, size_t len)
    {
#if defined(__LP64__)
        return MemSet<uint64_t>(dest, value, len);
#elif defined(__ILP32__)
        return MemSet<uint32_t>(dest, value, len);
#else
    #error "wtf are you compiling for"
#endif
    }

    void* MemMove(void* dest, const void* src, size_t len)
    {
        uint8_t* di = static_cast<uint8_t*>(dest);
        const uint8_t* si = static_cast<const uint8_t*>(src);

        for (size_t i = len; i > 0; i--)
            di[i - 1] = si[i - 1];

        return dest;
    }

    int MemCompare(const void* lhs, const void* rhs, size_t count)
    {
        const uint8_t* const l = static_cast<const uint8_t*>(lhs);
        const uint8_t* const r = static_cast<const uint8_t*>(rhs);

        for (size_t i = 0; i < count; i++)
        {
            if (l[i] > r[i])
                return 1;
            else if (r[i] > l[i])
                return -1;
        }

        return 0;
    }

    size_t MemFind(const void* buff, uint8_t target, size_t limit)
    {
        const uint8_t* s = static_cast<const uint8_t*>(buff);

        for (size_t i = 0; i < limit; i++)
        {
            if (s[i] == target)
                return i;
        }

        return limit;
    }
}

extern "C"
{
    void* memcpy(void* dest, const void* src, size_t len)
    {
        return sl::MemCopy(dest, src, len);
    }

    void* memset(void* dest, int value, size_t len)
    {
        return sl::MemSet(dest, value, len);
    }

    void* memmove(void* dest, const void* src, size_t len)
    {
        return sl::MemMove(dest, src, len);
    }
}
