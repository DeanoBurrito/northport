#include <drivers/api/Api.h>

[[gnu::used, gnu::section(".npk_module")]]
const npk_module_metadata moduleMetadata
{
    .metadata_start_guid = NP_MODULE_META_START_GUID,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .api_ver_rev = NP_MODULE_API_VER_REV,
};

void entry();

[[gnu::section(".npk_module")]]
const char fname0[] = "Test driver, please ignore.";
[[gnu::section(".npk_module")]]
const char fname1[] = "Another test driver, totally safe.";
[[gnu::section(".npk_module")]]
const uint8_t loadStr[] = { 0, 1, 2, 3, 4, 5, 6, 7, };

[[gnu::used, gnu::section(".npk_module")]]
const npk_driver_manifest driverManifest
{
    .manifest_start_guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 2,
    .ver_rev = 3,
    .load_type = npk_load_type::Always,
    .load_str = loadStr,
    .friendly_name = fname0,
    .entry = entry,
};

[[gnu::used, gnu::section(".npk_module")]]
const npk_driver_manifest extraManifest
{
    .manifest_start_guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 4,
    .ver_minor = 2,
    .ver_rev = 0,
    .load_type = npk_load_type::Never,
    .load_str = loadStr,
    .friendly_name = fname1,
    .entry = entry,
};

void entry()
{
    npk_log("Hello from a linked kernel driver!", npk_log_level::Info);

    npk_thread_exit(0);
    while (true)
    {}
}

