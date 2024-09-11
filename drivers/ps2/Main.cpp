#include <interfaces/driver/Api.h>
#include <Log.h>
#include <Controller.h>
#include <Keyboard.h>

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type_init:
        return Ps2::InitController();
    case npk_event_type_add_device:
        return Ps2::InitKeyboard(static_cast<npk_event_add_device*>(arg));
    default:
        Log("Unhandled event type: %u", LogLevel::Error, type);
        return false;
    }
}

NPK_METADATA const char keyboardStr[] = "PNP0303";
NPK_METADATA const npk_load_name loadNames[] = 
{
    NPK_PNP_ID_STR(keyboardStr),
};
NPK_METADATA const char friendlyName[] = "ps2";
NPK_METADATA const npk_driver_manifest manifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 0,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .flags = 0,
    .process_event = ProcessEvent,
    .friendly_name_len = sizeof(friendlyName),
    .friendly_name = friendlyName,
    .load_name_count = sizeof(loadNames) / sizeof(npk_load_name),
    .load_names = loadNames,
};
