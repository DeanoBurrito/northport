#include <NameLookup.h>
#include <interfaces/driver/Filesystem.h>
#include <Log.h>
#include <ApiStrings.h>
#include <VmObject.h>

namespace Pci
{
    bool lookupInit = false;
    dl::VmObject idsFile;

    void InitNameLookup()
    {
        if (lookupInit)
            return;

        const auto id = npk_fs_lookup("/initdisk/pci.ids"_apistr);
        VALIDATE(id.device_id != NPK_INVALID_HANDLE, , "Failed to open PCI ids file");

        lookupInit = true;
    }

    sl::String PciClassToName(uint32_t id)
    {
        const uint16_t vendor = id & 0xFFFF;
        const uint16_t device = id >> 16;

        //TODO: peruse file for matching id
        return {};
    }
}
