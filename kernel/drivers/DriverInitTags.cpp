#include <drivers/DriverInitTags.h>

namespace Kernel::Drivers
{
    sl::Opt<DriverInitTag*> DriverInitInfo::FindTag(DriverInitTagType type, DriverInitTag* start)
    {
        if (start == nullptr)
            start = next;
        
        while (start != nullptr)
        {
            if (start->type == type)
                return start;
            start = start->next;
        }

        return {};
    }
}
