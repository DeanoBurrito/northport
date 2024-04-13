#include <debug/Log.h>
#include <debug/Panic.h>
#include <drivers/DriverManager.h>
#include <interfaces/driver/Api.h>
#include <interfaces/Helpers.h>
#include <Memory.h>

extern "C"
{
    using namespace Npk;

    DRIVER_API_FUNC
    void npk_log(REQUIRED const char* str, npk_log_level level)
    {
        auto driver = Drivers::DriverManager::Global().GetShadow();
        const char* driverName = driver.Valid() ? driver->manifest->friendlyName.C_Str() : "unknown";

        Log("(driver:%s) %s", static_cast<LogLevel>(level), driverName, str);
    }

    DRIVER_API_FUNC
    void npk_panic(REQUIRED const char* why)
    {
        const size_t whyLength = sl::memfirst(why, 0, 0);
        Debug::Panic({ why, whyLength });
        ASSERT_UNREACHABLE();
    }
}
