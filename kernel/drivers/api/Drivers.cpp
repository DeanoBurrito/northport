#include <drivers/api/Drivers.h>
#include <drivers/DriverManager.h>
#include <debug/Log.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Drivers;

    [[gnu::used]]
    bool npk_add_device_api(npk_device_api* api)
    {
        auto shadow = DriverManager::Global().GetShadow();
        ASSERT_(shadow.Valid());

        return DriverManager::Global().AddApi(api, shadow);
    }

    [[gnu::used]]
    bool npk_remove_device_api(size_t device_id)
    {
        return DriverManager::Global().RemoveApi(device_id);
    }

    [[gnu::used]]
    bool npk_set_transport_api(npk_handle api_id)
    {
        auto shadow = DriverManager::Global().GetShadow();
        ASSERT_(shadow.Valid());

        return DriverManager::Global().SetTransportApi(shadow, api_id);
    }
}
