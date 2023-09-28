#include <drivers/api/Api.h>
#include <drivers/api/Scheduling.h>

void entry();

NPK_METADATA const npk_module_metadata moduleMetadata
{
    .guid = NP_MODULE_META_START_GUID,
    .api_ver_major = NP_MODULE_API_VER_MAJOR,
    .api_ver_minor = NP_MODULE_API_VER_MINOR,
    .api_ver_rev = NP_MODULE_API_VER_REV,
};

NPK_METADATA const char fname0[] = "Test driver, please ignore.";
NPK_METADATA const char fname1[] = "Another test driver, totally safe.";
NPK_METADATA const uint8_t loadStr[] = { 0, 1, 2, 3, 4, 5, 6, 7, };
NPK_METADATA const uint8_t loadStr2[] = { 0, 1, 2, 3 };

NPK_METADATA const npk_driver_manifest driverManifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 1,
    .ver_minor = 2,
    .ver_rev = 3,
    .load_type = npk_load_type::Always,
    .load_str_len = sizeof(loadStr),
    .load_str = loadStr,
    .friendly_name = fname0,
    .entry = entry,
};

NPK_METADATA const npk_driver_manifest extraManifest
{
    .guid = NP_MODULE_MANIFEST_GUID,
    .ver_major = 4,
    .ver_minor = 2,
    .ver_rev = 0,
    .load_type = npk_load_type::Never,
    .load_str_len = sizeof(loadStr),
    .load_str = loadStr2,
    .friendly_name = fname1,
    .entry = entry,
};

void entry()
{
    npk_log("Hello from a linked kernel driver!", npk_log_level::Info);

    //npk_thread_exit(0);
    while (true)
    {}
}

