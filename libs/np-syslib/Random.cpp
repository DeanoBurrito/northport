#include <Random.h>

namespace sl
{
    uint64_t XoshiroRng::initState = 0x6E6F727468706F72;

    uint64_t XoshiroRng::NextInitValue()
    {
        //vanilla splitmix64 algorithm
        uint64_t z = (initState += 0x9e3779b97f4a7c15);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }

    XoshiroRng::XoshiroRng()
    {
        state[0] = NextInitValue();
        state[1] = NextInitValue();
        state[2] = NextInitValue();
        state[3] = NextInitValue();
    }

    uint64_t XoshiroRng::Next()
    {
        auto Rotl = [](const uint64_t x, int k) 
        {
            return (x << k) | (x >> (64 - k));
        };

        const uint64_t result = Rotl(state[1] * 5, 7) * 9;
        const uint64_t t = state[1] << 17;

        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];

        state[2] ^= t;
        state[3] = Rotl(state[3], 45);
        return result;
    }
}
