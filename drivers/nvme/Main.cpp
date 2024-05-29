#include <interfaces/driver/Api.h>
#include <containers/LinkedList.h>
#include <Log.h>
#include <Controller.h>

sl::LinkedList<Nvme::NvmeController> controllers;

bool NvmeProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::Init:
        return true;
    case npk_event_type::AddDevice:
        {
            auto& controller = controllers.EmplaceBack();
            const bool success = controller.Init(static_cast<npk_event_add_device*>(arg));
            if (!success)
                controller.Deinit();
            return success;
        }
    default:
        Log("Unknown event type %u, ignoring.", LogLevel::Warning, type);
        return false;
    }

    ASSERT_UNREACHABLE();
}

NPK_METADATA const uint8_t loadName[] = NPK_PCI_CLASS_LOAD_STR(1, 8, 2);
NPK_METADATA const npk_load_name loadNames[] =
{
    { .type = npk_load_type::PciClass, .length = sizeof(loadName), .str = loadName }
};
NPK_METADATA const char friendlyName[] = "nvme";
NPK_METADATA const npk_driver_manifest driverManifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 0,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .flags = 0,
    .process_event = NvmeProcessEvent,
    .friendly_name_len = sizeof(friendlyName),
    .friendly_name = friendlyName,
    .load_name_count = sizeof(loadNames) / sizeof(npk_load_name),
    .load_names = loadNames,
};
