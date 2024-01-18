#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <interfaces/driver/Api.h>

extern "C"
{
    using namespace Npk;

    [[gnu::used]]
    void npk_log(REQUIRED const char* str, npk_log_level level)
    {
        auto driver = Drivers::DriverManager::Global().GetShadow();
        ASSERT_(driver.Valid());

        Log("(driver:%s) %s", static_cast<LogLevel>(level), driver->manifest->friendlyName.C_Str(), str);
    }

    [[gnu::used]]
    void npk_panic(REQUIRED const char* why)
    {
        Panic(why);
        ASSERT_UNREACHABLE();
    }
}
