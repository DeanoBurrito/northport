#include <NameLookup.h>
#include <interfaces/driver/Filesystem.h>
#include <Log.h>
#include <NanoPrintf.h>
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
        if (id.device_id == NPK_INVALID_HANDLE)
        {
            Log("Failed to open /initdisk/pci.ids file, using unfriendly names", LogLevel::Warning);
            return;
        }

        dl::VmFileArg fileArg {};
        fileArg.id = id;
        idsFile = dl::VmObject(1406885, fileArg, dl::VmFlag::File);
        if (!idsFile.Valid())
            Log("Failed to map /initdisk/pci.ids file.", LogLevel::Error);

        lookupInit = true;
    }

    static void SkipLine(sl::Span<const uint8_t> file, size_t& i)
    {
        while (i < file.Size() && file[i] != '\n')
            i++;
    }

    sl::String PciClassToName(uint32_t id)
    {
        if (!lookupInit || !idsFile.Valid())
            return {};

        const uint16_t vendor = id & 0xFFFF;
        const uint16_t device = id >> 16;

        /* Big disclaimer about string handling here: the pci.ids file has always been ascii, and 
         * I'm assuming that it remains that way here so we make assumptions about 1 char == 1 byte.
         */
        char vendorStr[5];
        npf_snprintf(vendorStr, 5, "%x", vendor);
        char deviceStr[5];
        npf_snprintf(deviceStr, 5, "%x", device);

        const char* vendorName = nullptr;
        const char* deviceName = nullptr;

        //TODO: cache vendor positions within the file
        auto file = idsFile.ConstSpan();
        for (size_t i = 0; i < file.Size(); i++)
        {
            if (file[i] == '#')
            {
                SkipLine(file, i);
                continue; //line is a comment
            }
            if (file[i] == 'C')
                return {}; //we've reached the end of the vendor/device ids.

            if (vendorName == nullptr)
            {
                //stage 1: we're looking for the vendor's name
                if (file[i] == '\t')
                {
                    SkipLine(file, i); //this line is a device name
                    continue;
                }
                if (sl::memcmp(file.Begin() + i, vendorStr, 4) != 0)
                {
                    SkipLine(file, i); //wrong vendor id
                    continue;
                }

                i += 6; //4 characters of vendor id + 2 padding spaces before vendor name.
                vendorName = reinterpret_cast<const char*>(file.Begin() + i);
                SkipLine(file, i);
                continue;
            }
            else
            {
                //stage 2: looking for the device's name
                if (file[i++] != '\t')
                    return {}; //reached end of vendor's device list, but didnt find our device.

                if (sl::memcmp(file.Begin() + i, deviceStr, 4) != 0)
                {
                    SkipLine(file, i);
                    continue;
                }
                i += 6;

                //piece together the vendor and device names and return it.
                const size_t vendorNameLen = sl::memfirst(vendorName, '\n', 0);
                deviceName = reinterpret_cast<const char*>(file.Begin() + i);
                const size_t deviceNameLen = sl::memfirst(deviceName, '\n', 0);

                const size_t nameLength = npf_snprintf(nullptr, 0, "%.*s %.*s", (int)vendorNameLen,
                    vendorName, (int)deviceNameLen, deviceName) + 1;
                char* nameBuff = new char[nameLength];
                if (nameBuff == nullptr)
                    return {};

                npf_snprintf(nameBuff, nameLength, "%.*s %.*s", (int)vendorNameLen, vendorName,
                    (int)deviceNameLen, deviceName);
                return nameBuff;
            }
        };

        return {};
    }
}
