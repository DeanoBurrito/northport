#include <interfaces/driver/Api.h>
#include <PciSegment.h>
#include <NameLookup.h>
#include <Log.h>
#include <containers/LinkedList.h>

sl::LinkedList<Pci::PciSegment> pciSegments;

static bool PciBusAccess(size_t width, uintptr_t addr, uintptr_t* data, bool write)
{
    const uint16_t segId = (addr >> 32);
    for (auto it = pciSegments.Begin(); it != pciSegments.End(); ++it)
    {
        if (it->Id() != segId)
            continue;

        return it->RawAccess(width, addr, data, write);
    }

    return false;
}

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::Init:
        Pci::InitNameLookup();
        return true;
    case npk_event_type::AddDevice:
        {
            auto event = static_cast<const npk_event_add_device*>(arg);
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
            if (!segment.Init(hostTag))
                return false;
            if (pciSegments.Size() == 1)
                return npk_add_bus_access(BusPci, PciBusAccess);
            return true;
        }
    default:
        Log("Unknown event type %u, ignoring.", LogLevel::Warning, type);
        return false;
    }
    ASSERT_UNREACHABLE();
}

NPK_METADATA const npk_load_name loadNames[] =
{
    { .type = npk_load_type::PciHost, .length = 0, .str = nullptr }
};
NPK_METADATA const char friendlyName[] = "pci";
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
    .load_names = loadNames,
};
