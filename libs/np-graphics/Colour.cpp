#include <Colour.h>

namespace np::Graphics
{
    uint32_t Colour::Pack(size_t ro, size_t go, size_t bo, size_t ao, uint8_t rm, uint8_t gm, uint8_t bm, uint8_t am) const
    {
        return (((uint32_t)r & rm) << ro) | (((uint32_t)g & gm) << go) | (((uint32_t)b & bm) << bo) | (((uint32_t)a & am) << ao);
    }
}
