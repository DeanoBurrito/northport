#include <interfaces/driver/Api.h>
#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/utilities.h>
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

            //TODO: install GPE handlers for things we want before this.
            if (auto status = uacpi_finalize_gpe_initialization(); status != UACPI_STATUS_OK)
            {
                Log("GPE init failed, ec: %lu (%s)", LogLevel::Error, (size_t)status,
                    uacpi_status_to_string(status));
                return false;
            }
            //TODO: install notify handler at root?

            return true;
        }

    default:
        return false;
    }
}

extern "C"
{
    int __popcountdi2(int64_t a)
    {
        /* This function was taken from https://github.com/mintsuki/cc-runtime, which is a rip
         * of the LLVM compiler runtime library (different flavour of libgcc).
         * See https://llvm.org/LICENSE.txt for the full license and more info.
         */
        uint64_t x2 = (uint64_t)a;
        x2 = x2 - ((x2 >> 1) & 0x5555555555555555uLL);
        x2 = ((x2 >> 2) & 0x3333333333333333uLL) + (x2 & 0x3333333333333333uLL);
        x2 = (x2 + (x2 >> 4)) & 0x0F0F0F0F0F0F0F0FuLL;
        uint32_t x = (uint32_t)(x2 + (x2 >> 32));
        x = x + (x >> 16);
        return (x + (x >> 8)) & 0x0000007F;
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
