#include <UnitConverter.h>

namespace sl
{
    const char* unitStrsDecimal[] = 
    {
        "", "k", "M", "G", "T", "P",
        "E", "Z", "Y"
    };

    const char* unitStrsBinary[] = 
    {
        "", "Ki", "Mi", "Gi", "Ti", "Pi",
        "Ei", "Zi", "Yi"
    };

    constexpr size_t unitStrsCount = 9;

    UnitConversion ConvertUnits(size_t input, UnitBase base)
    {
        const size_t div = (size_t)base;

        UnitConversion conv { input, 0, 0 };
        size_t count = 0;

        while (conv.major >= div && count < unitStrsCount)
        {
            conv.minor = conv.major % div;
            conv.major /= div;
            count++;
        }

        conv.prefix = base == UnitBase::Decimal ? unitStrsDecimal[count] : unitStrsBinary[count];
        return conv;
    }
}
