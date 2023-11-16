#include <drivers/api/Api.h>
#include <drivers/api/Scheduling.h>
#include <containers/Vector.h>
#include <Log.h>
#include <GraphicsAdaptor.h>

sl::Vector<QemuVga::GraphicsAdaptor> gpus;

void DriverEntry()
{
    npk_thread_exit(0);
}

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::AddDevice:
        {
            auto event = static_cast<const npk_event_new_device*>(arg);
            auto& gpu = gpus.EmplaceBack();
            
            const npk_init_tag* scan = event->tags;
            while (scan != nullptr)
            {
                if (scan->type == npk_init_tag_type::PciFunction)
                    break;
                scan = scan->next;
            }

            VALIDATE(scan != nullptr, false, "QemuVga needs a PCI address to initialize.");
            auto pciTag = reinterpret_cast<const npk_init_tag_pci_function*>(scan);
            return gpu.Init(pciTag);
        }
    default:
        Log("Unknown event type %u, ignoring.", LogLevel::Warning, type);
        return false;
    }

    ASSERT_UNREACHABLE();
}

NPK_METADATA const npk_module_metadata moduleMetadata
{
    .guid = NP_MODULE_META_START_GUID,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .api_ver_rev = NP_MODULE_API_VER_REV,
};

NPK_METADATA const char friendlyName[] = "qemu_vga";
NPK_METADATA const uint8_t loadStr[] = { 0x34, 0x12, 0x11, 0x11 };
NPK_METADATA const npk_driver_manifest driverManifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 0,
    .ver_rev = 0,
    .load_type = npk_load_type::PciId,
    .load_str_len = sizeof(loadStr),
    .load_str = loadStr,
    .friendly_name = friendlyName,
    .entry = DriverEntry,
    .process_event = ProcessEvent
};
