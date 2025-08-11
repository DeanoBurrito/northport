#include <UnitConverter.hpp>

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

    constexpr size_t unitStrsCount = sizeof(unitStrsBinary) / sizeof(const char*);
    static_assert(sizeof(unitStrsDecimal) == sizeof(unitStrsBinary));

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

        if (base == UnitBase::Decimal)
            conv.prefix = unitStrsDecimal[count];
        else 
        {
            if (base == UnitBase::Binary)
                conv.prefix = unitStrsBinary[count];
            else
                conv.prefix = "?";

            //re-scale minor component so it fits nicely (e.g. 12.1023 looks odd compared to 12.999)
            conv.minor = conv.minor * 1000 / div;
        }
        return conv;
    }
}
