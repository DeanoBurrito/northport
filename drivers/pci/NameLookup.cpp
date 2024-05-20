#include <NameLookup.h>
#include <interfaces/driver/Filesystem.h>
#include <Log.h>
#include <NanoPrintf.h>
#include <ApiStrings.h>
#include <VmObject.h>

/* A big disclaimer if you're here hunting a bug: this code assumes that the pci.ids file is
 * plain ascii where 1 char is exactly 1 byte.
 */
namespace Pci
{
    struct CachedVendorLookup
    {
        const char* name;
        size_t startIndex;
        uint16_t id;
    };

    bool lookupInit = false;
    dl::VmObject idsFile;
    size_t classNamesStart;

    sl::Vector<CachedVendorLookup> cachedVendorNames;

    void InitNameLookup()
    {
        if (lookupInit)
            return;
        classNamesStart = -1ul;

        const auto id = npk_fs_lookup("/initdisk/pci.ids"_apistr);
        if (id.device_id == NPK_INVALID_HANDLE)
        {
            Log("Failed to open /initdisk/pci.ids file, using unfriendly names", LogLevel::Warning);
            return;
        }

        dl::VmFileArg fileArg {};
        fileArg.id = id;
        idsFile = dl::VmObject(1406885, fileArg, dl::VmFlag::File); //TODO: dont hardcode file size, fetch it from vfs
        if (!idsFile.Valid())
            Log("Failed to map /initdisk/pci.ids file.", LogLevel::Error);
        else
        {
            //the ids file puts the class descriptions at the end of the file, so in order to avoid loading
            //the entire file at once, we search backwards until we find the first class description.
            auto file = idsFile.ConstSpan();
            for (size_t ri = file.Size() - 1; ri != 0; ri--)
            {
                if (file[ri] != 'C')
                    continue;
                if (sl::memcmp(file.Begin() + ri, "C 00", 4) != 0)
                    continue;

                classNamesStart = ri;
                break;
            }
        }

        lookupInit = true;
    }

    static void SkipLine(sl::Span<const uint8_t> file, size_t& i)
    {
        while (i < file.Size() && file[i] != '\n')
            i++;
    }

    static sl::String GetNameFromId(uint32_t id)
    {
        const uint16_t vendor = id & 0xFFFF;
        const uint16_t device = id >> 16;

        char vendorStr[5];
        npf_snprintf(vendorStr, 5, "%x", vendor);
        char deviceStr[5];
        npf_snprintf(deviceStr, 5, "%x", device);

        const char* vendorName = nullptr;
        const char* deviceName = nullptr;
        size_t searchStartIndex = 0;

        //check if we've previously located this vendor within the file
        for (auto it = cachedVendorNames.Begin(); it != cachedVendorNames.End(); ++it)
        {
            if (it->id != vendor)
                continue;
            vendorName = it->name;
            searchStartIndex = it->startIndex;
            break;
        }

        auto file = idsFile.ConstSpan();
        for (size_t i = searchStartIndex; i < file.Size(); i++)
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

                //cache the position of this vendor within the file, if we've detected one piece of hardware from
                //them, its likely there's others floating around the system.
                auto& cacheEntry = cachedVendorNames.EmplaceBack();
                cacheEntry.startIndex = i + 1;
                cacheEntry.name = vendorName;
                cacheEntry.id = vendor;
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

    static sl::String GetNameFromClass(uint32_t pciClass)
    {
        char classStr[3];
        npf_snprintf(classStr, 3, "%02x", (pciClass >> 24) & 0xFF);
        char subclassStr[3];
        npf_snprintf(subclassStr, 3, "%02x", (pciClass >> 16) & 0xFF);
        char progIfStr[3];
        npf_snprintf(progIfStr, 3, "%02x", (pciClass >> 8) & 0xFF);
        const char* targets[] = { classStr, subclassStr, progIfStr };

        const char* nameStr = nullptr;
        size_t currentDepth = 0;

        auto file = idsFile.ConstSpan();
        for (size_t i = classNamesStart; i < file.Size(); i++)
        {
            if (file[i] == '#')
            {
                SkipLine(file, i);
                continue; //line is a comment
            }

            size_t scanDepth = 0;
            while (file[i + scanDepth] == '\t')
                scanDepth++;
            if (scanDepth < currentDepth)
                break;

            if (currentDepth == 0 && file[i] == 'C')
                i += 2;

            if (sl::memcmp(file.Begin() + i, targets[currentDepth], 2) != 0)
            {
                SkipLine(file, i);
                continue;
            }

            i += 4;
            nameStr = reinterpret_cast<const char*>(file.Begin() + i);
            currentDepth++;
            SkipLine(file, i);
        }

        return {};
    }

    sl::String PciClassToName(uint32_t id, uint32_t pciClass)
    {
        if (!lookupInit || !idsFile.Valid())
            return {};

        if (auto vendorName = GetNameFromId(id); !vendorName.IsEmpty())
            return vendorName;
        if (auto className = GetNameFromClass(pciClass); !className.IsEmpty())
            return className;
        return {}; //everything failed, return nothing.
    }
}
