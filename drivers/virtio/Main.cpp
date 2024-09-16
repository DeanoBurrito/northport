#include <Gpu.h>
#include <interfaces/driver/Api.h>
#include <Log.h>

static bool AddNewDevice(void* arg)
{
    //TODO: determine which device we should init, for now we can hardcode this
    Virtio::Gpu* gpu = new Virtio::Gpu();
    if (!gpu->transport.Init(static_cast<npk_event_add_device*>(arg)))
    {
        delete gpu;
        return false;
    }

    if (!gpu->Init())
    {
        gpu->transport.Shutdown();
        delete gpu;
        return false;
    }
    //TODO: stash device pointer for later access
    return true;
}

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type_init:
        return true;
    case npk_event_type_add_device:
        return AddNewDevice(arg);
    default:
        Log("Unknown event type: %u", LogLevel::Error, type);
        return false;
    }
}

NPK_METADATA const uint8_t gpuPciId[] = NPK_PCI_ID_LOAD_STR(0x1AF4, 0x1050);
NPK_METADATA const npk_load_name loadNames[] = 
{
    { .type = npk_load_type_pci_id, .length = sizeof(gpuPciId), .str = gpuPciId },
};
NPK_METADATA const char friendlyName[] = "virtio";
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

