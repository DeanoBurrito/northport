#include <drivers/DriverManager.h>
#include <debug/Log.h>
#include <interfaces/driver/Drivers.h>
#include <interfaces/Helpers.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Drivers;

    DRIVER_API_FUNC
    bool npk_add_device_api(npk_device_api* api)
    {
        auto shadow = DriverManager::Global().GetShadow();
        VALIDATE_(shadow.Valid(), false);

        return DriverManager::Global().AddApi(api, shadow);
    }

    DRIVER_API_FUNC
    bool npk_remove_device_api(size_t device_id)
    {
        return DriverManager::Global().RemoveApi(device_id);
    }

    DRIVER_API_FUNC
    bool npk_set_transport_api(npk_handle api_id)
    {
        auto shadow = DriverManager::Global().GetShadow();
        VALIDATE_(shadow.Valid(), false);

        return DriverManager::Global().SetTransportApi(shadow, api_id);
    }
}
