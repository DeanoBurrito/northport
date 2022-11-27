#pragma once

#include <stdint.h>

namespace sl
{
    /*
        This PRNG and is based on the work by David Blackman and Sebastiano Vigna (vigna@acm.org).
        The initial state is provided by splitmix64, also written by Sebastiano Vigna in 2015.
        Their original source is released as public domain, this software is distributed without any warranty.

        This implementation is the xoshiro256** version (https://prng.di.unimi.it/xoshiro256starstar.c)
    */
    class XoshiroRng
    {
    private:
        static uint64_t initState; //splitmix64 state, used for initializing main rng
        uint64_t state[4];

        uint64_t NextInitValue();
    public:
        XoshiroRng();

        uint64_t Next();
    };
}
