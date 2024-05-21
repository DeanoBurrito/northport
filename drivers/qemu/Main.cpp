#include <interfaces/driver/Api.h>
#include <containers/Vector.h>
#include <Log.h>
#include <GraphicsAdaptor.h>
#include <Power.h>

sl::Vector<Qemu::GraphicsAdaptor> gpus;

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::Init:
        return true;
    case npk_event_type::AddDevice:
        {
            Qemu::InitPowerDevice(); //TODO: we cant register APIs in the INIT event

            auto event = static_cast<const npk_event_add_device*>(arg);
            auto& gpu = gpus.EmplaceBack();
            return gpu.Init(event);
        }
    default:
        Log("Unknown event type %u, ignoring.", LogLevel::Warning, type);
        return false;
    }

    ASSERT_UNREACHABLE();
}

NPK_METADATA const uint8_t loadName[] = NPK_PCI_ID_LOAD_STR(0x1234, 0x1111);
NPK_METADATA const npk_load_name loadNames[] =
{
    { .type = npk_load_type::PciId, .length = sizeof(loadName), .str = loadName }
};
NPK_METADATA const char friendlyName[] = "qemu";
NPK_METADATA const npk_driver_manifest driverManifest
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
    .load_names = loadNames
};
