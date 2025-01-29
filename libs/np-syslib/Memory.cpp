#include <Memory.h>
#include <Compiler.h>

extern "C"
{
    /* This implementation of memcpy() is from the managarm kernel (see https://github.com/managarm/managarm),
     * and is also MIT-licensed. The license text follows, but I also want to say thanks
     * to the managarm team (if any of you ever read this) for this code.
     *
     *  ---------------------------------------
     * Copyright 2014-2020 Managarm Contributors
     *
     * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
     *
     *  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
     *
     *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
     *
     */
    extern "C++" 
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
    }

#if defined(__LP64__)
    void* memcpy(void* dest, const void* src, size_t len)
    {
        auto curDest = reinterpret_cast<unsigned char*>(dest);
        auto curSrc = reinterpret_cast<const unsigned char*>(src);

        while (len >= 8 * 8)
        {
            auto w0 = AliasLoad<uint64_t>(curSrc);
            auto w1 = AliasLoad<uint64_t>(curSrc);
            auto w2 = AliasLoad<uint64_t>(curSrc);
            auto w3 = AliasLoad<uint64_t>(curSrc);
            auto w4 = AliasLoad<uint64_t>(curSrc);
            auto w5 = AliasLoad<uint64_t>(curSrc);
            auto w6 = AliasLoad<uint64_t>(curSrc);
            auto w7 = AliasLoad<uint64_t>(curSrc);
            AliasStore<uint64_t>(curDest, w0);
            AliasStore<uint64_t>(curDest, w1);
            AliasStore<uint64_t>(curDest, w2);
            AliasStore<uint64_t>(curDest, w3);
            AliasStore<uint64_t>(curDest, w4);
            AliasStore<uint64_t>(curDest, w5);
            AliasStore<uint64_t>(curDest, w6);
            AliasStore<uint64_t>(curDest, w7);

            len -= 8 * 8;
        }
        
        if (len >= 4 * 8)
        {
            auto w0 = AliasLoad<uint64_t>(curSrc);
            auto w1 = AliasLoad<uint64_t>(curSrc);
            auto w2 = AliasLoad<uint64_t>(curSrc);
            auto w3 = AliasLoad<uint64_t>(curSrc);
            AliasStore<uint64_t>(curDest, w0);
            AliasStore<uint64_t>(curDest, w1);
            AliasStore<uint64_t>(curDest, w2);
            AliasStore<uint64_t>(curDest, w3);

            len -= 4 * 8;
        }

        if (len >= 2 * 8)
        {
            auto w0 = AliasLoad<uint64_t>(curSrc);
            auto w1 = AliasLoad<uint64_t>(curSrc);
            AliasStore<uint64_t>(curDest, w0);
            AliasStore<uint64_t>(curDest, w1);

            len -= 2 * 8;
        }

        if (len >= 8)
        {
            auto w0 = AliasLoad<uint64_t>(curSrc);
            AliasStore<uint64_t>(curDest, w0);

            len -= 8;
        }

        if (len >= 4)
        {
            auto w0 = AliasLoad<uint32_t>(curSrc);
            AliasStore<uint32_t>(curDest, w0);

            len -= 4;
        }

        if (len >= 2)
        {
            auto w0 = AliasLoad<uint16_t>(curSrc);
            AliasStore<uint16_t>(curDest, w0);

            len -= 2;
        }

        if (len > 0)
            *curDest = *curSrc;

        return dest;
    }
#elif defined(__ILP32__)
    void* memcpy(void* dest, const void* src, size_t len)
    {
        auto curDest = reinterpret_cast<unsigned char*>(dest);
        auto curSrc = reinterpret_cast<const unsigned char*>(src);

        //TODO: investigate interaction with cache line size and gpr count here
        while (len >= 8 * 4)
        {
            auto w0 = AliasLoad<uint32_t>(curSrc);
            auto w1 = AliasLoad<uint32_t>(curSrc);
            auto w2 = AliasLoad<uint32_t>(curSrc);
            auto w3 = AliasLoad<uint32_t>(curSrc);
            auto w4 = AliasLoad<uint32_t>(curSrc);
            auto w5 = AliasLoad<uint32_t>(curSrc);
            auto w6 = AliasLoad<uint32_t>(curSrc);
            auto w7 = AliasLoad<uint32_t>(curSrc);
            AliasStore<uint32_t>(curDest, w0);
            AliasStore<uint32_t>(curDest, w1);
            AliasStore<uint32_t>(curDest, w2);
            AliasStore<uint32_t>(curDest, w3);
            AliasStore<uint32_t>(curDest, w4);
            AliasStore<uint32_t>(curDest, w5);
            AliasStore<uint32_t>(curDest, w6);
            AliasStore<uint32_t>(curDest, w7);

            len -= 8 * 4;
        }
        
        if (len >= 4 * 4)
        {
            auto w0 = AliasLoad<uint32_t>(curSrc);
            auto w1 = AliasLoad<uint32_t>(curSrc);
            auto w2 = AliasLoad<uint32_t>(curSrc);
            auto w3 = AliasLoad<uint32_t>(curSrc);
            AliasStore<uint32_t>(curDest, w0);
            AliasStore<uint32_t>(curDest, w1);
            AliasStore<uint32_t>(curDest, w2);
            AliasStore<uint32_t>(curDest, w3);

            len -= 4 * 4;
        }

        if (len >= 2 * 4)
        {
            auto w0 = AliasLoad<uint32_t>(curSrc);
            auto w1 = AliasLoad<uint32_t>(curSrc);
            AliasStore<uint32_t>(curDest, w0);
            AliasStore<uint32_t>(curDest, w1);

            len -= 2 * 4;
        }

        if (len >= 4)
        {
            auto w0 = AliasLoad<uint32_t>(curSrc);
            AliasStore<uint32_t>(curDest, w0);

            len -= 4;
        }

        if (len >= 2)
        {
            auto w0 = AliasLoad<uint16_t>(curSrc);
            AliasStore<uint16_t>(curDest, w0);

            len -= 2;
        }

        if (len > 0)
            *curDest = *curSrc;

        return dest;
    }
#else

    void* memcpy(void* dest, const void* src, size_t len)
    { 
        uint8_t* d = static_cast<uint8_t*>(dest);
        const uint8_t* s = static_cast<const uint8_t*>(src);

        for (size_t i = 0; i < len; i++)
            d[i] = s[i];

        return dest;
    }
#endif

    void* memset(void* dest, int value, size_t len)
    {
        uint8_t* d = static_cast<uint8_t*>(dest);

        for (size_t i = 0; i < len; i++)
            d[i] = value;

        return dest;
    }

    void* memmove(void* dest, const void* src, size_t len)
    {
        uint8_t* di = static_cast<uint8_t*>(dest);
        const uint8_t* si = static_cast<const uint8_t*>(src);

        for (size_t i = len; i > 0; i--)
            di[i - 1] = si[i - 1];

        return dest;
    }
}

namespace sl
{
    void* MemCopy(void* dest, const void* src, size_t len)
    {
        return memcpy(dest, src, len);
    }

    void* MemSet(void* dest, int value, size_t len)
    {
        return memset(dest, value, len);
    }

    void* MemMove(void* dest, const void* src, size_t len)
    {
        return memmove(dest, src, len);
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
