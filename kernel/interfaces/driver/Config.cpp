#include <interfaces/driver/Config.h>
#include <interfaces/Helpers.h>
#include <config/ConfigStore.h>

extern "C"
{
    using namespace Npk;

    DRIVER_API_FUNC
    npk_string npk_get_config(npk_string key)
    {
        const sl::StringSpan span(key.data, key.length);
        const sl::StringSpan value = Config::GetConfig(span);

        return npk_string { .length = value.Size(), .data = value.Begin() };
    }

    DRIVER_API_FUNC
    size_t npk_get_config_num(npk_string key, size_t or_default)
    {
        const sl::StringSpan span(key.data, key.length);

        return Config::GetConfigNumber(span, or_default);
    }
}
