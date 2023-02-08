#include <config/DeviceTree.h>
#include <boot/LimineExtensions.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>

/*
    As far as FDT (DTB) parsers go, this one is on the long side. However it makes zero
    memory allocations, which allows it to be used extremely early in the boot sequence.
*/
namespace Npk::Config
{
    constexpr uint32_t FdtMagic = 0xD00DFEED;
    constexpr uint32_t FdtBeginNode = 0x1;
    constexpr uint32_t FdtEndNode = 0x2;
    constexpr uint32_t FdtProp = 0x3;
    constexpr uint32_t FdtNop = 0x4;
    constexpr uint32_t FdtEnd = 0x9;

    struct FdtHeader
    {
        uint32_t magic;
        uint32_t totalSize;
        uint32_t offsetDtStruct;
        uint32_t offsetDtStrings;
        uint32_t offsetMemRsvmap;
        uint32_t version;
        uint32_t lastCompVersion;
        uint32_t bootCpuidPhys;
        uint32_t sizeDtStrings;
        uint32_t sizeDtStruct;
    };

    struct FdtReserveEntry
    {
        uint64_t address;
        uint64_t length;
    };

    struct FdtProperty
    {
        uint32_t length;
        uint32_t nameOffset; //offset into strings table
    };

//yes, this is named BS very deliberately. 
//This is what I will call your standard if you make it big-endian by default.
#define BS(x) sl::ByteSwap(x)

    sl::Opt<const DtNode> DeviceTree::FindCompatibleHelper(const DtNode& scan, const char* compatStr, size_t compatStrLen, size_t start) const
    {
        //NOTE: two loops are used because I want to search width-first.
        for (size_t i = 0; i < scan.childCount; i++)
        {
            const DtNode child = *GetChild(scan, i);
            if (child.ptr <= start)
                continue;

            auto maybeProp = GetProp(child, "compatible");
            if (!maybeProp)
                continue;
            
            size_t propStrIndex = 0;
            for (const char* propStr = ReadStr(*maybeProp, propStrIndex); propStr != nullptr; propStr = ReadStr(*maybeProp, ++propStrIndex))
            {
                const size_t propNameLen = sl::memfirst(propStr, 0, 0);
                if (propNameLen != compatStrLen)
                    continue;
                if (sl::memcmp(propStr, compatStr, compatStrLen) != 0)
                    continue;
                return child;
            }
        }

        for (size_t i = 0; i < scan.childCount; i++)
        {
            auto found = FindCompatibleHelper(*GetChild(scan, i), compatStr, compatStrLen, start);
            if (found)
                return *found;
        }

        return {};
    }

    size_t DeviceTree::SkipNode(size_t start) const
    {
        if (BS(cells[start]) == FdtBeginNode)
            start++;
        
        size_t depth = 0;
        for (size_t i = start; i < cellsCount; i++)
        {
            switch (BS(cells[i]))
            {
            case FdtBeginNode:
            {
                const size_t nameLen = sl::memfirst(cells + i + 1, 0, 0);
                i += (sl::AlignUp(nameLen + 1, 4) / 4);
                depth++;
                continue;
            }
            case FdtProp:
            {
                i++;
                const FdtProperty* prop = reinterpret_cast<const FdtProperty*>(cells + i);
                i += (sl::AlignUp(BS(prop->length), 4) / 4) + 1;
                continue;
            }
            case FdtEndNode:
                if (depth == 0)
                    return i;
                else
                    depth--;
                continue;
            }
        }

        return cellsCount;
    }

    DtNode DeviceTree::CreateNode(size_t start, size_t addrCells, size_t sizeCells) const
    {
        constexpr size_t AddrCellNameLen = 14;
        constexpr size_t SizeCellNameLen = 11;
        
        DtNode node;
        node.ptr = start;
        node.sizeCells = sizeCells;
        node.addrCells = addrCells;
        node.childAddrCells = 2;
        node.childSizeCells = 1;
        node.childCount = node.propCount = 0;
        node.propPtr = node.childPtr = 0;

        node.name = reinterpret_cast<const char*>(cells + start + 1);
        const size_t nodeNameLen = sl::memfirst(node.name, 0, 0);
        start += (sl::AlignUp(nodeNameLen + 1, 4) / 4) + 1;

        for (size_t i = start; i < cellsCount; i++)
        {
            switch (BS(cells[i]))
            {
            case FdtBeginNode:
                if (node.childPtr == 0)
                    node.childPtr = i;
                node.childCount++;
                i = SkipNode(i);
                break;
            case FdtProp:
            {
                if (node.propPtr == 0)
                    node.propPtr = i;
                node.propCount++;
                i++;
                const FdtProperty* prop = reinterpret_cast<const FdtProperty*>(cells + i);
                const char* propName = reinterpret_cast<const char*>(strings + BS(prop->nameOffset));
                const size_t propNameLen = sl::memfirst(propName, 0, 0);

                if (propNameLen == AddrCellNameLen && sl::memcmp("#address-cells", propName, propNameLen) == 0)
                    node.childAddrCells = BS(*(cells + i + 2));
                else if (propNameLen == SizeCellNameLen && sl::memcmp("#size-cells", propName, propNameLen) == 0)
                    node.childSizeCells = BS(*(cells + i + 2));

                i += (sl::AlignUp(BS(prop->length), 4) / 4) + 1;
                break;
            }
            case FdtEndNode:
                return node;
            }
        }

        return node;
    }

    DtProperty DeviceTree::CreateProperty(size_t start) const
    {
        const FdtProperty* property = reinterpret_cast<const FdtProperty*>(cells + start + 1);

        DtProperty prop;
        prop.ptr = start;
        prop.name = reinterpret_cast<const char*>(strings + BS(property->nameOffset));

        return prop;
    }

    DeviceTree globalDt;
    DeviceTree& DeviceTree::Global()
    { return globalDt; }

    void DeviceTree::Init(uintptr_t dtbAddr)
    {
        const FdtHeader* header = reinterpret_cast<FdtHeader*>(dtbAddr);
        ASSERT(BS(header->magic) == FdtMagic, "DTB header mismatch.");

        strings = reinterpret_cast<uint8_t*>((uintptr_t)header + BS(header->offsetDtStrings));
        cells = reinterpret_cast<const uint32_t*>((uintptr_t)header + BS(header->offsetDtStruct));
        cellsCount = BS(header->sizeDtStruct) / sizeof(uint32_t);

        for (size_t i = 0; i < cellsCount; i++)
        {
            if (BS(cells[i]) != FdtBeginNode)
                continue;
            rootNode = CreateNode(i, 2, 1); //defaults of 2 address cells + 1 size cell, as per spec.
            break;
        }

        Log("Device tree parser initialized: dtb=0x%lx, rootCell=%lu", LogLevel::Info, (uintptr_t)header, rootNode.ptr);
    }

    bool DeviceTree::Available()
    {
        return cellsCount != 0;
    }

    sl::Opt<const DtNode> DeviceTree::GetNode(const char* path) const
    {
        if (cellsCount == 0)
            return {};

        const size_t pathLen = sl::memfirst(path, 0, 0);
        if (pathLen == 0)
            return {};
        if (path[pathLen - 1] == '/')
        {
            if (pathLen == 1)
                return rootNode;
            Log("Device tree paths cannot end with '/'", LogLevel::Error);
            return {};
        }

        size_t segStart = path[0] == '/' ? 1 : 0;
        size_t segLen = sl::memfirst(path, segStart, '/', pathLen);
        DtNode node = rootNode;
        bool final = false;

        while (true)
        {
            if (segLen == (size_t)-1)
            {
                final = true;
                segLen = pathLen - segStart;
            }
            else
                segLen -= segStart;

            bool foundNext = false;
            for (size_t i = 0; i < node.childCount; i++)
            {
                DtNode child;
                { 
                    auto maybeChild = GetChild(node, i);
                    ASSERT(maybeChild, "Invalid DT child index.");
                    child = *maybeChild;
                }
                const size_t childNameLen = sl::memfirst(child.name, 0, 0);
                if (childNameLen < segLen)
                    continue;
                if (sl::memcmp(child.name, &path[segStart], segLen) != 0)
                    continue;
                
                foundNext = true;
                node = child;

                if (final)
                    return node;
            }
            if (!foundNext || final)
                return {};

            segStart += segLen + 1;
            segLen = sl::memfirst(path, segStart, '/', pathLen);
        }

        return {};
    }

    sl::Opt<const DtNode> DeviceTree::GetCompatibleNode(const char* compatStr, sl::Opt<const DtNode> start) const
    {
        if (cellsCount == 0)
            return {};

        const size_t compatStrLen = sl::memfirst(compatStr, 0, 0);
        return FindCompatibleHelper(rootNode, compatStr, compatStrLen, start ? start->ptr : rootNode.ptr);
    }

    sl::Opt<const DtNode> DeviceTree::GetChild(const DtNode& parent, const char* name) const
    {
        if (cellsCount == 0 || parent.childPtr == 0)
            return {};

        const size_t nameLen = sl::memfirst(name, 0, 0);
        for (size_t i = 0; i < parent.childCount; i++)
        {
            DtNode child;
            { 
                auto maybeChild = GetChild(parent, i);
                ASSERT(maybeChild, "Invalid DT child index.");
                child = *maybeChild;
            }
            const size_t childNameLen = sl::memfirst(child.name, 0, 0);

            if (childNameLen <= nameLen)
                continue;
            if (sl::memcmp(child.name, name, nameLen) != 0)
                continue;
            return child;
        }

        return {};
    }
    
    sl::Opt<const DtNode> DeviceTree::GetChild(const DtNode& parent, size_t index) const
    {
        if (cellsCount == 0 || parent.childCount <= index || parent.childPtr == 0)
            return {};

        ASSERT(BS(cells[parent.ptr]) == FdtBeginNode, "Invalid DtNode pointer")
        ASSERT(BS(cells[parent.childPtr]) == FdtBeginNode, "Invalid DtNode child pointer")
        
        size_t child = 0;
        for (size_t i = parent.childPtr; i < cellsCount; i++)
        {
            if (BS(cells[i]) == FdtNop)
                continue;
            if (BS(cells[i]) == FdtEndNode)
                break;

            if (BS(cells[i]) == FdtProp)
            {
                i++;
                i += sizeof(FdtProperty) / sizeof(uint32_t);
                const FdtProperty* prop = reinterpret_cast<const FdtProperty*>(cells + i);
                i += (sl::AlignUp(BS(prop->length), 4) / 4) - 1;
                continue;
            }

            if (BS(cells[i]) == FdtBeginNode)
            {
                if (child < index)
                {
                    i = SkipNode(i);
                    child++;
                }
                else
                    return CreateNode(i, parent.childAddrCells, parent.childSizeCells);
            }
        }

        return {};
    }

    sl::Opt<const DtProperty> DeviceTree::GetProp(const DtNode& node, const char* name) const
    {
        if (cellsCount == 0 || node.propPtr == 0)
            return {};
        
        const size_t nameLen = sl::memfirst(name, 0, 0);
        for (size_t i = 0; i < node.propCount; i++)
        {
            DtProperty prop;
            { 
                auto maybeProp = GetProp(node, i);
                ASSERT(maybeProp, "Invalid DT property index.");
                prop = *maybeProp;
            }
            const size_t propNameLen = sl::memfirst(prop.name, 0, 0);

            if (propNameLen != nameLen)
                continue;
            if (sl::memcmp(prop.name, name, nameLen) != 0)
                continue;
            return prop;
        }

        return {};
    }

    sl::Opt<const DtProperty> DeviceTree::GetProp(const DtNode& node, size_t index) const
    {
        if (cellsCount == 0 || node.propCount <= index || node.propPtr == 0)
            return {};

        ASSERT(BS(cells[node.ptr]) == FdtBeginNode, "Invalid DtNode pointer.");
        ASSERT(BS(cells[node.propPtr]) == FdtProp, "Invalid DtProperty pointer.");

        size_t propIndex = 0;
        for (size_t i = node.propPtr; i < cellsCount; i++)
        {
            if (BS(cells[i]) != FdtProp)
                break;
            
            const FdtProperty* prop = reinterpret_cast<const FdtProperty*>(cells + i + 1);
            if (propIndex == index)
                return CreateProperty(i);
            
            propIndex++;
            i += (sl::AlignUp(BS(prop->length), 4) / 4) + 2;
        }

        return {};
    }

    const char* DeviceTree::ReadStr(const DtProperty& prop, size_t index) const
    { 
        if (cellsCount == 0)
            return {};
        
        ASSERT(BS(cells[prop.ptr]) == FdtProp, "Invalid DtProperty pointer.");
        const FdtProperty* property = reinterpret_cast<const FdtProperty*>(cells + prop.ptr + 1);
        const uint8_t* charCells = reinterpret_cast<const uint8_t*>(cells);
        
        size_t currIndex = 0;
        for (size_t scan = (prop.ptr + 3) * 4; scan < (prop.ptr + 3) * 4 + BS(property->length);)
        {
            if (*(charCells + scan) == 0)
                break;

            if (currIndex == index)
                return reinterpret_cast<const char*>(charCells + scan);
            
            currIndex++;
            scan += sl::memfirst(charCells + scan, 0, 0) + 1;
        }

        return nullptr;
    }

    uint32_t DeviceTree::ReadNumber(const DtProperty& prop) const
    { 
        if (cellsCount == 0)
            return {};
        
        ASSERT(BS(cells[prop.ptr]) == FdtProp, "Invalid DtProperty pointer.");
        
        return BS(*(cells + prop.ptr + 3));
    }

    size_t DeviceTree::ReadRegs(const DtNode& node, const DtProperty& prop, uintptr_t* bases, size_t* lengths) const
    { 
        if (cellsCount == 0)
            return {};
        
        return ReadPairs(prop, node.addrCells, node.sizeCells, bases, lengths);
    }

    size_t DeviceTree::ReadPairs(const DtProperty& prop, size_t aCells, size_t bCells, size_t* aStore, size_t* bStore) const
    {
        if (cellsCount == 0)
            return {};
        
        ASSERT(BS(cells[prop.ptr]) == FdtProp, "Invalid DtProperty pointer.");
        
        auto Extract = [&](size_t where, size_t cellCount)
        {
            size_t value = 0;
            for (size_t i = 0; i < cellCount; i++)
                value |= (size_t)BS(cells[where + i]) << ((cellCount - 1 - i) * 32);
            return value;
        };

        const FdtProperty* property = reinterpret_cast<const FdtProperty*>(cells + prop.ptr + 1);
        const size_t count = BS(property->length) / ((aCells + bCells) * 4);
        if (aStore == nullptr && bStore == nullptr)
            return count;

        const uintptr_t propertyData = prop.ptr + 3;
        for (size_t i = 0; i < count; i++)
        {
            const size_t readBase = propertyData + i * (aCells + bCells);
            if (aStore != nullptr)
                aStore[i] = Extract(readBase, aCells);
            if (bStore != nullptr)
                bStore[i] = Extract(readBase + aCells, bCells);
        }
        
        return count;
    }

    size_t DeviceTree::ReadRanges(const DtNode& node, const DtProperty& prop, uintptr_t* parents, uintptr_t* children, size_t* lengths) const
    {
        if (cellsCount == 0)
            return 0;

        ASSERT(BS(cells[prop.ptr]) == FdtProp, "Invalid DtProperty pointer.");

        auto Extract = [&](size_t where, size_t cellCount)
        {
            size_t value = 0;
            for (size_t i = 0; i < cellCount; i++)
                value |= (size_t)BS(cells[where + i]) << ((cellCount - 1 - i) * 32);
            return value;
        };

        /*  This function is designed with PCI ranges prop in mind:
            The address may be formed of 3 32bit ints, with the upper int
            containing hint data about the device. We ignore the hint data
            as we get it from the device itself.

            When it comes to encoding within cells, the following is used:
            - node.address_cells is used for child addresses
            - node.size_cells is used for the length
            - parent.address_cells is used for parent addresses
        */

        const size_t entryLength = node.childSizeCells + node.childAddrCells + node.addrCells;
        const FdtProperty* property = reinterpret_cast<const FdtProperty*>(cells + prop.ptr + 1);
        const size_t count = BS(property->length) / (entryLength * 4);
        if (parents == nullptr && children == nullptr && lengths == nullptr)
            return count;
        
        const uintptr_t data = prop.ptr + 3;
        const size_t readOffset = node.childAddrCells - (sizeof(uintptr_t) / 4);
        static_assert(sizeof(uintptr_t) % 4 == 0, "wot");

        for (size_t i = 0; i < count; i++)
        {
            const size_t readBase = data + i * entryLength;
            if (children != nullptr)
                children[i] = Extract(readBase + readOffset, node.childAddrCells - readOffset);
            if (parents != nullptr)
                parents[i] = Extract(readBase + node.childAddrCells, node.addrCells);
            if (lengths != nullptr)
                lengths[i] = Extract(readBase + node.childAddrCells + node.addrCells, node.childSizeCells);
        }

        return count;
    }
}
