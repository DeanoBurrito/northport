#include <interfaces/driver/Api.h>
#include <uacpi/uacpi.h>
#include <Log.h>

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type::Init:
        return true;
    case npk_event_type::AddDevice:
        {
            uacpi_init_params params {};
            params.rt_params.log_level = UACPI_LOG_TRACE;
            params.no_acpi_mode = false;
            params.rsdp = 0;

            auto event = static_cast<const npk_event_add_device*>(arg);
            auto scan = event->tags;
            while (scan != nullptr)
            {
                if (scan->type != npk_init_tag_type::Rsdp)
                {
                    scan = scan->next;
                    continue;
                }

                params.rsdp = (uintptr_t)reinterpret_cast<const npk_init_tag_rsdp*>(scan)->rsdp;
                break;
            }
            if (params.rsdp == 0)
                return false;

            if (auto status = uacpi_initialize(&params); status != UACPI_STATUS_OK)
            {
                Log("Library init failed, ec: %lu (%s)", LogLevel::Error, (size_t)status, 
                    uacpi_status_to_string(status));
                return false;
            }

            if (auto status = uacpi_namespace_load(); status != UACPI_STATUS_OK)
            {
                Log("Namespace loading failed, ec: %lu (%s)", LogLevel::Error, (size_t)status, 
                    uacpi_status_to_string(status));
                return false;
            }

            if (auto status = uacpi_namespace_initialize(); status != UACPI_STATUS_OK)
            {
                Log("Namespace init failed, ec: %lu (%s)", LogLevel::Error, (size_t)status, 
                    uacpi_status_to_string(status));
                return false;
            }
            //TODO: install notify handler at root?
            //TODO: expose some apis to the kernel, we can at least enumerate devices.

            return true;
        }

    default:
        return false;
    }
}

NPK_METADATA const npk_module_metadata moduleMetadata
{
    .guid = NP_MODULE_META_START_GUID,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .api_ver_rev = NP_MODULE_API_VER_REV,
};

NPK_METADATA const char friendlyName[] = "uacpi";
NPK_METADATA const npk_driver_manifest manifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 0,
    .ver_rev = 0,
    .load_type = npk_load_type::AcpiRuntime,
    .load_str_len = 0,
    .load_str = nullptr,
    .friendly_name = friendlyName,
    .process_event = ProcessEvent
};
