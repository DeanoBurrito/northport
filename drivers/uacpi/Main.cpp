#include <interfaces/driver/Api.h>
#include <interfaces/driver/Devices.h>
#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/utilities.h>
#include <Log.h>
#include <Memory.h>

static size_t Utf16ToAscii(const void* lameMicrosoftString, size_t msStringLen, void* outputBuffer)
{
    if (outputBuffer == nullptr)
        return msStringLen / 2;

    const uint16_t* inPtr = static_cast<const uint16_t*>(lameMicrosoftString);
    uint8_t* outPtr = static_cast<uint8_t*>(outputBuffer);

    for (size_t i = 0; i < msStringLen; i++)
        outPtr[i] = static_cast<uint8_t>(inPtr[i]);

    return msStringLen / 2;
}

static void SetPnpDescriptorName(uacpi_namespace_node* node, npk_device_desc* desc)
{
    //acpi has some built-in ways of getting human readable device names, in order we try:
    //_MLS: seems to be the modern approach, provides multiple languages.
    //_STR: provides a single string, also fine for us.
    //_DDN: is the DOS device name, not really its intended use but it works as a nicer name than 'PNP69420'

    uacpi_object* nameObj = nullptr;
    if (uacpi_eval_typed(node, "_MLS", nullptr, UACPI_OBJECT_PACKAGE_BIT, &nameObj) == UACPI_STATUS_OK)
    {
        //TODO: parse MLS packages
    }
    else if (uacpi_eval_typed(node, "_STR", nullptr, UACPI_OBJECT_BUFFER_BIT, &nameObj) == UACPI_STATUS_OK)
    {
        const size_t buffLen = Utf16ToAscii(nullptr, nameObj->buffer->size, nullptr);
        char* buff = new char[buffLen];
        VALIDATE_(buff != nullptr, );

        Utf16ToAscii(nameObj->buffer->data, nameObj->buffer->size, buff);
        buff[buffLen - 1] = 0;
        desc->friendly_name.data = buff;
        desc->friendly_name.length = buffLen - 1;
    }
    else if (uacpi_eval_typed(node, "_DDN", nullptr, UACPI_OBJECT_STRING_BIT, &nameObj) == UACPI_STATUS_OK)
    {
        const size_t buffLen = Utf16ToAscii(nullptr, nameObj->buffer->size, nullptr);
        char* buff = new char[buffLen];
        VALIDATE_(buff != nullptr, );

        Utf16ToAscii(nameObj->buffer->data, nameObj->buffer->size, buff);
        buff[buffLen - 1] = 0;
        desc->friendly_name.data = buff;
        desc->friendly_name.length = buffLen - 1;
    }
}

uacpi_ns_iteration_decision NamespaceEnumerator(void* user, uacpi_namespace_node* node)
{
    (void)user;

    //run some checks on each acpi namespace node, to determine if its
    //suitable for registering a device descriptor.
    uacpi_namespace_node_info* info = nullptr;
    if (uacpi_get_namespace_node_info(node, &info) != UACPI_STATUS_OK)
        return UACPI_NS_ITERATION_DECISION_CONTINUE;

    if (info->type != UACPI_OBJECT_DEVICE)
    {
        uacpi_free_namespace_node_info(info);
        return UACPI_NS_ITERATION_DECISION_CONTINUE;
    }

    size_t nameCount = 0;
    if (info->flags & UACPI_NS_NODE_INFO_HAS_HID)
        nameCount++;
    if (info->flags & UACPI_NS_NODE_INFO_HAS_CID)
        nameCount += info->cid.num_ids;
    if (nameCount == 0)
    {
        uacpi_free_namespace_node_info(info);
        return UACPI_NS_ITERATION_DECISION_CONTINUE;
    }

    //the node meets our requirements, create the device descriptor to pass
    //to the kernel. We create a load_name for the _HID object, and
    //one for each _CID value we get.
    npk_load_name* names = new npk_load_name[nameCount];
    ASSERT_(names != nullptr);

    //_HID
    names[0].type = npk_load_type_acpi_pnp;
    names[0].length = info->hid.size - 1;
    uint8_t* strBuff = new uint8_t[info->hid.size];
    ASSERT_(strBuff != nullptr);
    sl::memcopy(info->hid.value, strBuff, info->hid.size);
    names[0].str = strBuff;

    //_CIDs, if present
    if (info->flags & UACPI_NS_NODE_INFO_HAS_CID)
    {
        for (size_t i = 0; i < info->cid.num_ids; i++)
        {
            names[i + 1].type = npk_load_type_acpi_pnp;
            names[i + 1].length = info->cid.ids[i].size - 1;
            strBuff = new uint8_t[info->hid.size];
            ASSERT_(strBuff != nullptr);
            sl::memcopy(info->hid.value, strBuff, info->hid.size);
            names[i + 1].str = strBuff;
        }
    }

    //create descriptor struct itself and handoff to kernel
    npk_device_desc* desc = new npk_device_desc();
    ASSERT_(desc != nullptr);
    desc->init_data = nullptr;
    desc->load_name_count = nameCount;
    desc->load_names = names;
    desc->driver_data = node;

    //default to using the HID as the friendly name
    desc->friendly_name.length = names[0].length;
    desc->friendly_name.data = reinterpret_cast<const char*>(names[0].str);
    SetPnpDescriptorName(node, desc);

    if (!npk_add_device_desc(desc))
    {
        Log("Failed to add pnp device descriptor: %.*s", LogLevel::Error, (int)names[0].length, names[0].str);
        delete desc;

        for (size_t i = 0; i < nameCount; i++)
            operator delete[]((void*)names[i].str, names[i].length);
        operator delete[](names, nameCount);
    }

    uacpi_free_namespace_node_info(info);
    return UACPI_NS_ITERATION_DECISION_CONTINUE;
}

bool ProcessEvent(npk_event_type type, void* arg)
{
    switch (type)
    {
    case npk_event_type_init:
        return true;
    case npk_event_type_add_device:
        {
            uacpi_init_params params {};
            params.log_level = UACPI_LOG_TRACE;
            params.rsdp = 0;

            auto event = static_cast<const npk_event_add_device*>(arg);
            auto scan = event->tags;
            while (scan != nullptr)
            {
                if (scan->type != npk_init_tag_type_rsdp)
                {
                    scan = scan->next;
                    continue;
                }

                params.rsdp = reinterpret_cast<const npk_init_tag_rsdp*>(scan)->rsdp;
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

            uacpi_namespace_for_each_node_depth_first(uacpi_namespace_root(),
                NamespaceEnumerator, nullptr);

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

NPK_METADATA const npk_load_name loadNames[] =
{
    { .type = npk_load_type_acpi_runtime, .length = 0, .str = nullptr }
};
NPK_METADATA const char friendlyName[] = "uacpi";
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
    .load_names = loadNames
};
