#include <stdint.h>

/* "Theres no such thing as a temporary solution"
 * - Someone wiser than me.
 */
extern "C"
{
    /* All the code within this `extern "C"` block is copied from 
     * https://github.com/mintsuki/cc-runtime, which is a stripped back
     * integer-only version of LLVM's compiler runtime library.
     * See https://llvm.org/LICENSE.txt for license details.
     */
    using su_int = uint32_t;
    using si_int = int32_t;
    using du_int = uint64_t;
    using di_int = int64_t;
    using fixuint_t = du_int;
    using fixint_t = di_int;

    union udwords
    {
        du_int all;

        struct {
#if __BYTE_ORDER__ == __ORDER_LITLE_ENDIAN__
            su_int low;
            su_int high;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            su_int high;
            su_int low;
#else
    #error "How did we get here"
#endif
        } s;
    };

#define CHAR_BIT 8
#define ctzsi(x) __builtin_ctz(x)
#define clzsi(x) __builtin_clz(x)

    int __clzsi2(si_int a)
    {
        su_int x = (su_int)a;
        si_int t = ((x & 0xFFFF0000) == 0) << 4;
        x >>= 16 - t;
        su_int r = t;
        t = ((x & 0xFF00) == 0) << 3;
        x >>= 8 - t;
        r += t;
        t = ((x & 0xF0) == 0) << 2;
        x >>= 4 - t;
        r += t;
        t = ((x & 0xC) == 0) << 1;
        x >>= 2 - t;
        r += t;
        return r + ((2 - x) & -((x & 2) == 0));
    }

    du_int __udivmoddi4(du_int a, du_int b, du_int *rem) 
    {
        const unsigned n_uword_bits = sizeof(su_int) * CHAR_BIT;
        const unsigned n_udword_bits = sizeof(du_int) * CHAR_BIT;
        udwords n;
        n.all = a;
        udwords d;
        d.all = b;
        udwords q;
        udwords r;
        unsigned sr;
        if (n.s.high == 0) 
        {
            if (d.s.high == 0) 
            {
                if (rem)
                    *rem = n.s.low % d.s.low;
                return n.s.low / d.s.low;
            }
            if (rem)
                *rem = n.s.low;
            return 0;
        }
        if (d.s.low == 0) 
        {
            if (d.s.high == 0) 
            {
                if (rem)
                    *rem = n.s.high % d.s.low;
                return n.s.high / d.s.low;
            }
            if (n.s.low == 0) 
            {
                if (rem) 
                {
                    r.s.high = n.s.high % d.s.high;
                    r.s.low = 0;
                    *rem = r.all;
                }
                return n.s.high / d.s.high;
            }
            if ((d.s.high & (d.s.high - 1)) == 0) 
            {
                if (rem) 
                {
                    r.s.low = n.s.low;
                    r.s.high = n.s.high & (d.s.high - 1);
                    *rem = r.all;
                }
                return n.s.high >> ctzsi(d.s.high);
            }
            sr = clzsi(d.s.high) - clzsi(n.s.high);
            if (sr > n_uword_bits - 2) 
            {
                if (rem)
                    *rem = n.all;
                return 0;
            }
            ++sr;
            q.s.low = 0;
            q.s.high = n.s.low << (n_uword_bits - sr);
            r.s.high = n.s.high >> sr;
            r.s.low = (n.s.high << (n_uword_bits - sr)) | (n.s.low >> sr);
        } 
        else  
        {
            if (d.s.high == 0) 
            {
                if ((d.s.low & (d.s.low - 1)) == 0) 
                {
                    if (rem)
                        *rem = n.s.low & (d.s.low - 1);
                    if (d.s.low == 1)
                        return n.all;
                    sr = ctzsi(d.s.low);
                    q.s.high = n.s.high >> sr;
                    q.s.low = (n.s.high << (n_uword_bits - sr)) | (n.s.low >> sr);
                    return q.all;
                }

                sr = 1 + n_uword_bits + clzsi(d.s.low) - clzsi(n.s.high);
                if (sr == n_uword_bits) 
                {
                    q.s.low = 0;
                    q.s.high = n.s.low;
                    r.s.high = 0;
                    r.s.low = n.s.high;
                } 
                else if (sr < n_uword_bits) 
                {
                    q.s.low = 0;
                    q.s.high = n.s.low << (n_uword_bits - sr);
                    r.s.high = n.s.high >> sr;
                    r.s.low = (n.s.high << (n_uword_bits - sr)) | (n.s.low >> sr);
                } 
                else 
                {
                    q.s.low = n.s.low << (n_udword_bits - sr);
                    q.s.high = (n.s.high << (n_udword_bits - sr)) |
                            (n.s.low >> (sr - n_uword_bits));
                    r.s.high = 0;
                    r.s.low = n.s.high >> (sr - n_uword_bits);
                }
            } 
            else 
            {
                sr = clzsi(d.s.high) - clzsi(n.s.high);
                if (sr > n_uword_bits - 1) 
                {
                    if (rem)
                        *rem = n.all;
                    return 0;
                }
                ++sr;
                q.s.low = 0;
                if (sr == n_uword_bits) 
                {
                    q.s.high = n.s.low;
                    r.s.high = 0;
                    r.s.low = n.s.high;
                } 
                else 
                {
                    q.s.high = n.s.low << (n_uword_bits - sr);
                    r.s.high = n.s.high >> sr;
                    r.s.low = (n.s.high << (n_uword_bits - sr)) | (n.s.low >> sr);
                }
            }
        }
        su_int carry = 0;
        for (; sr > 0; --sr) {
            r.s.high = (r.s.high << 1) | (r.s.low >> (n_uword_bits - 1));
            r.s.low = (r.s.low << 1) | (q.s.high >> (n_uword_bits - 1));
            q.s.high = (q.s.high << 1) | (q.s.low >> (n_uword_bits - 1));
            q.s.low = (q.s.low << 1) | carry;
            const di_int s = (di_int)(d.all - r.all - 1) >> (n_udword_bits - 1);
            carry = s & 1;
            r.all -= d.all & s;
        }

        q.all = (q.all << 1) | carry;
        if (rem)
            *rem = r.all;
        return q.all;
    }

    static inline fixuint_t __udivXi3(fixuint_t n, fixuint_t d) 
    {
        const unsigned N = sizeof(fixuint_t) * CHAR_BIT;
        unsigned sr = (d ? __clzsi2(d) : N) - (n ? __clzsi2(n) : N);
        if (sr > N - 1)
            return 0;
        if (sr == N - 1)
            return n;
        ++sr;
        fixuint_t r = n >> sr;
        n <<= N - sr;
        fixuint_t carry = 0;
        for (; sr > 0; --sr) 
        {
            r = (r << 1) | (n >> (N - 1));
            n = (n << 1) | carry;
            const fixint_t s = (fixint_t)(d - r - 1) >> (N - 1);
            carry = s & 1;
            r -= d & s;
        }
        n = (n << 1) | carry;
        return n;
    }

    static inline fixuint_t __umodXi3(fixuint_t n, fixuint_t d)
    {
        const unsigned N = sizeof(fixuint_t) * CHAR_BIT;
        unsigned sr = (d ? __clzsi2(d) : N) - (n ? __clzsi2(n) : N);
        if (sr > N - 1)
            return n;
        if (sr == N - 1)
            return 0;
        ++sr;
        fixuint_t r = n >> sr;
        n <<= N - sr;
        fixuint_t carry = 0;
        for (; sr > 0; --sr) {
            r = (r << 1) | (n >> (N - 1));
            n = (n << 1) | carry;
            const fixint_t s = (fixint_t)(d - r - 1) >> (N - 1);
            carry = s & 1;
            r -= d & s;
        }
        return r;
    }

    static inline fixint_t __divXi3(fixint_t a, fixint_t b) 
    {
        const int N = (int)(sizeof(fixint_t) * CHAR_BIT) - 1;
        fixint_t s_a = a >> N;
        fixint_t s_b = b >> N;
        fixuint_t a_u = (fixuint_t)(a ^ s_a) + (-s_a);
        fixuint_t b_u = (fixuint_t)(b ^ s_b) + (-s_b);
        s_a ^= s_b;
        return (__udivmoddi4(a_u, b_u, (du_int*)0) ^ s_a) + (-s_a);
    }

    static inline fixint_t __modXi3(fixint_t a, fixint_t b) 
    {
        const int N = (int)(sizeof(fixint_t) * CHAR_BIT) - 1;
        fixint_t s = b >> N;
        fixuint_t b_u = (fixuint_t)(b ^ s) + (-s);
        s = a >> N;
        fixuint_t a_u = (fixuint_t)(a ^ s) + (-s);
        fixuint_t res;
        __udivmoddi4(a_u, b_u, &res);
        return (res ^ s) + (-s);
    }

    di_int __divdi3(di_int a, di_int b) { return __divXi3(a, b); }
    di_int __moddi3(di_int a, di_int b) { return __modXi3(a, b); }
    du_int __udivdi3(du_int a, du_int b) { return __udivXi3(a, b); }
    du_int __umoddi3(du_int a, du_int b) { return __umodXi3(a, b); }
}
