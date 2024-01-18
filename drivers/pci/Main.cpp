#include <interfaces/driver/Api.h>
#include <PciSegment.h>
#include <NameLookup.h>
#include <Log.h>
#include <containers/LinkedList.h>

sl::LinkedList<Pci::PciSegment> pciSegments;

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::AddDevice:
        {
            //ensure we've tried to open the ids file.
            Pci::InitNameLookup();

            auto event = static_cast<const npk_event_new_device*>(arg);
            auto& segment = pciSegments.EmplaceBack();

            const npk_init_tag* scan = event->tags;
            while (scan != nullptr)
            {
                if (scan->type == npk_init_tag_type::PciHostAdaptor)
                    break;
                scan = scan->next;
            }

            VALIDATE_(scan != nullptr, false);
            auto hostTag = reinterpret_cast<const npk_init_tag_pci_host*>(scan);
            return segment.Init(hostTag);
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

NPK_METADATA const char friendlyName[] = "pci";
NPK_METADATA const uint8_t loadStr[] = {};
NPK_METADATA const npk_driver_manifest driverManifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 0,
    .ver_rev = 0,
    .load_type = npk_load_type::PciHost,
    .load_str_len = sizeof(loadStr),
    .load_str = loadStr,
    .friendly_name = friendlyName,
    .process_event = ProcessEvent
};
