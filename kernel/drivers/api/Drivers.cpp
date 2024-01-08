#include <drivers/api/Drivers.h>
#include <drivers/DriverManager.h>
#include <debug/Log.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Drivers;

    [[gnu::used]]
    bool npk_add_device_api(npk_device_api* api, npk_handle parent)
    {
        auto shadow = DriverManager::Global().GetShadow();
        ASSERT_(shadow.Valid());

        return DriverManager::Global().AddApi(api, parent, shadow);
    }

    [[gnu::used]]
    bool npk_remove_device_api(size_t device_id)
    {
        return DriverManager::Global().RemoveApi(device_id);
    }
}
