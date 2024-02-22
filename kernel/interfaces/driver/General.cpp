#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <interfaces/driver/Api.h>
#include <interfaces/Helpers.h>
#include <stdarg.h>

extern "C"
{
    using namespace Npk;

    DRIVER_API_FUNC
    void npk_log(REQUIRED const char* str, npk_log_level level)
    {
        auto driver = Drivers::DriverManager::Global().GetShadow();
        ASSERT_(driver.Valid());

        Log("(driver:%s) %s", static_cast<LogLevel>(level), driver->manifest->friendlyName.C_Str(), str);
    }

    DRIVER_API_FUNC
    void npk_panic(REQUIRED const char* why)
    {
        Panic(why);
        ASSERT_UNREACHABLE();
    }
}
