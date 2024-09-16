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
    void npk_log(npk_string str, npk_log_level level)
    {
        auto driver = Drivers::DriverManager::Global().GetShadow();
        const char* driverName = driver.Valid() ? driver->manifest->friendlyName.C_Str() : "unknown";

        Log("(driver:%s) %.*s", static_cast<LogLevel>(level), driverName, (int)str.length, str.data);
    }

    static sl::RwLock accessLock;
    static sl::Vector<bool (*)(size_t width, uintptr_t addr, uintptr_t* data, bool write)> accessFuncs;

    DRIVER_API_FUNC
    bool npk_add_bus_access(npk_bus_type type, bool (*func)(size_t width, uintptr_t addr, uintptr_t* data, bool write))
    {
        bool success = false;
        accessLock.WriterLock();
        if (type >= accessFuncs.Size() || accessFuncs[type] == nullptr)
        {
            accessFuncs.EmplaceAt(type, func);
            success = true;
        }
        accessLock.WriterUnlock();

        return success;
    }

    DRIVER_API_FUNC
    bool npk_access_bus(npk_bus_type type, size_t width, uintptr_t addr, REQUIRED uintptr_t* data, bool write)
    {
        //TODO: this function feels like a bit of a hack, design a better way to accomplish this
        bool success = false;
        accessLock.ReaderLock();
        if (type < accessFuncs.Size() && accessFuncs[type] != nullptr)
            success = accessFuncs[type](width, addr, data, write);
        accessLock.ReaderUnlock();

        return success;
    }
}
