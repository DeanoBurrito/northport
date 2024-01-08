#include <drivers/api/Drivers.h>
#include <drivers/DriverManager.h>
#include <debug/Log.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Drivers;

    [[gnu::used]]
    npk_handle npk_add_device_desc(REQUIRED OWNING npk_device_desc* descriptor, bool as_child)
    {
        VALIDATE_(descriptor != nullptr, NPK_INVALID_HANDLE);

        return DriverManager::Global().AddDescriptor(descriptor);
    }

    [[gnu::used]]
    bool npk_remove_device_desc(npk_handle which, OPTIONAL void** driver_data)
    {
        const auto result = DriverManager::Global().RemoveDescriptor(which);
        if (result.HasValue())
        {
            if (driver_data == nullptr)
            {
                Log("Non-null driver data pointer (device %lu), but no storage provided to return it.",
                    LogLevel::Warning, which);
            }
            else
                *driver_data = *result;
        }

        return result.HasValue();
    }
}
