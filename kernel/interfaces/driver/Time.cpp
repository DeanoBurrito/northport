#include <interfaces/driver/Time.h>
#include <interfaces/Helpers.h>
#include <tasking/Clock.h>

extern "C"
{
    using namespace Npk::Tasking;

    DRIVER_API_FUNC
    npk_monotonic_time npk_get_monotonic_time()
    {
        const auto uptime = GetUptime();

        npk_monotonic_time monoTime {};
        monoTime.resolution = 1;
        monoTime.ticks = uptime.units;
        monoTime.frequency = static_cast<size_t>(uptime.scale);

        return monoTime;
    }
}
