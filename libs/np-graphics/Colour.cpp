#include <Colour.h>

namespace np::Graphics
{
    uint32_t Colour::Pack(size_t ro, size_t go, size_t bo, size_t ao, uint8_t rm, uint8_t gm, uint8_t bm, uint8_t am) const
    {
        return (r & rm) << ro | (g & gm) << go | (b & bm) << bo | (a & am) << ao;
    }
}
