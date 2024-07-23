#include <config/DeviceTree.h>
#include <config/DeviceTreeDefs.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <NativePtr.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Config
{
    //byte-swap for little endian hosts, or however else you choose to interpret the
    //naming.
    constexpr uint32_t Bs(uint32_t input)
    {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return input;
#else
        return sl::ByteSwap(input);
#endif
    }

    constexpr DtValue ExtractCells(const uint32_t* cells, size_t count)
    {
        DtValue value = 0;
        for (size_t i = 0; i < count; i++)
            value |= (DtValue)Bs(cells[i]) << ((count - 1 - i) * 32);
        return value;
    }

    DtValue DtProp::ReadValue(size_t cellCount) const
    {
        return ExtractCells(firstCell, cellCount);
    }

    sl::StringSpan DtProp::ReadString(size_t index) const
    {
        auto charCells = reinterpret_cast<const char*>(firstCell);
        const size_t charLength = length * (FdtCellSize / sizeof(char));

        size_t stringIndex = 0;
        for (size_t i = 0; i < charLength; i++)
        {
            if (charCells[i] == 0)
            {
                stringIndex++;
                continue;
            }
            else if (stringIndex < index)
                continue;

            const size_t length = sl::memfirst(charCells+ i, 0, charLength- i);
            return { charCells + i, length };
        }

        return {};
    }

    static void DoReadValues(const uint32_t* base, size_t count, size_t depth, DtValue* layout, sl::Span<DtValue> values)
    {
        size_t stride = 0;
        for (size_t i = 0; i < depth; i++)
            stride += layout[depth];

        //TODO: error prone and not smart, replace this with a manual version
        for (size_t i = 0; i < count; i++)
        {
            for (size_t j = 0; j < depth; j++)
                values[(i * depth) + j] = ExtractCells(base, layout[j]);
            base += stride;
        }
    }

    size_t DtProp::ReadValues(size_t cellCount, sl::Span<DtValue> values) const
    {
        if (cellCount == 0)
            return 0;

        const size_t count = length / (cellCount * FdtCellSize);
        if (values.Empty())
            return count;

        for (size_t i = 0; i < count; i++)
        {
            const uint32_t* base = firstCell + (i * cellCount);
            values[i] = ExtractCells(base, cellCount);
        }

        return count;
    }

    size_t DtProp::ReadPairs(DtPair layout, sl::Span<DtPair> values) const
    {
        if (layout[0] == 0 || layout[1] == 0)
            return 0;

        const size_t stride = layout[0] + layout[1];
        const size_t count = length / (stride * FdtCellSize);
        if (values.Empty())
            return count;

        sl::Span<DtValue> convValues(values.Begin()[0], values.SizeBytes() / sizeof(DtValue));
        DoReadValues(firstCell, count, 2, layout, convValues);
        return count;
    }

    size_t DtProp::ReadTriplets(DtTriplet layout, sl::Span<DtTriplet> values) const
    {
        if (layout[0] == 0 || layout[1] == 0 || layout[2] == 0)
            return 0;

        const size_t stride = layout[0] + layout[1] + layout[2];
        const size_t count = length / (stride * FdtCellSize);
        if (values.Empty())
            return count;

        sl::Span<DtValue> convValues(values.Begin()[0], values.SizeBytes() / sizeof(DtValue));
        DoReadValues(firstCell, count, 3, layout, convValues);
        return count;
    }

    size_t DtProp::ReadQuads(DtQuad layout, sl::Span<DtQuad> values) const
    {
        if (layout[0] == 0 || layout[1] == 0 || layout[2] == 0 || layout[3] == 0)
            return 0;

        const size_t stride = layout[0] + layout[1] + layout[2] + layout[3];
        const size_t count = length / (stride * FdtCellSize);
        if (values.Empty())
            return count;

        sl::Span<DtValue> convValues(values.Begin()[0], values.SizeBytes() / sizeof(DtValue));
        DoReadValues(firstCell, count, 4, layout, convValues);
        return count;
    }

    size_t DtProp::ReadRegs(sl::Span<DtPair> values) const
    {
        if (node == nullptr || node->Parent() == nullptr)
            return 0;

        DtPair layout 
        { 
            node->Parent()->AddrCells(), 
            node->Parent()->SizeCells()
        };

        return ReadPairs(layout, values);
    }

    size_t DtProp::ReadRanges(sl::Span<DtTriplet> values) const
    {
        if (node == nullptr || node->Parent() == nullptr)
            return 0;

        DtTriplet layout
        {
            node->AddrCells(),
            node->Parent()->AddrCells(),
            node->SizeCells()
        };

        return ReadTriplets(layout, values);
    }

    size_t DtProp::ReadRangesWithMeta(sl::Span<DtQuad> values, size_t metadataCells) const
    {
        if (node == nullptr || node->Parent() == nullptr)
            return 0;

        DtQuad layout
        {
            node->AddrCells(),
            node->Parent()->AddrCells(),
            node->SizeCells(),
            metadataCells
        };

        return ReadQuads(layout, values);
    }

    DtNode* DtNode::FindChild(sl::StringSpan name) const
    {
        (void)name;
        ASSERT_UNREACHABLE();
    }

    DtProp* DtNode::FindProp(sl::StringSpan name) const
    {
        (void)name;
        ASSERT_UNREACHABLE();
    }

    void DeviceTree::CheckForHooks(DtNode* node, DtProp* prop)
    {
        const char name0 = prop->name[0];
        if (name0 != '#' || name0 != 'p' || name0 != 'l')
            return; //short-circuit logic

        constexpr sl::StringSpan phandle("phandle");
        constexpr sl::StringSpan lhandle("linux,phandle");
        constexpr sl::StringSpan addrCells("#address-cells");
        constexpr sl::StringSpan sizeCells("#size-cells");

        if (prop->name == phandle || prop->name == lhandle)
        {
            phandleLookup[prop->ReadValue(1)] = node;
            return;
        }
        else if (prop->name == addrCells)
        {
            node->addrCells = prop->ReadValue(1);
            return;
        }
        else if (prop->name == sizeCells)
        {
            node->sizeCells = prop->ReadValue(1);
            return;
        }
    }

    DtNode* DeviceTree::ParseNode(size_t& i, uint8_t addrCells, uint8_t sizeCells)
    {
        if (Bs(cells[i]) != FdtBeginNode)
            return nullptr;

        ASSERT(nodeBuffRemain > 0, "Unable to allocate DtNode");
        DtNode* node = nodeBuff;
        nodeBuff++;
        nodeBuffRemain--;

        const char* name = reinterpret_cast<const char*>(cells + i + 1);
        const size_t nameLen = sl::memfirst(name, 0, cellCount - i);
        ASSERT(nameLen < cellCount - i, "Node name not terminated");

        node->sizeCells = sizeCells;
        node->addrCells = addrCells;
        node->name = sl::StringSpan(name, nameLen);
        node->parent = node->child = node->sibling = nullptr;
        node->props = nullptr;

        //consume leading token and name from parsing state
        i += (sl::AlignUp(nameLen + 1, FdtCellSize) / FdtCellSize) + 1;

        //parse child nodes and properties
        while (i < cellCount)
        {
            const uint32_t token = Bs(cells[i]);
            if (token == FdtEndNode)
            {
                //end of node, stop recursive parsing here
                i++;
                return node;
            }
            else if (token == FdtBeginNode)
            {
                //there's a child node, parse it
                DtNode* child = ParseNode(i, addrCells, sizeCells);
                ASSERT(child != nullptr, "Bad FdtBeginNode token");
                child->sibling = node->child;
                node->child = child;
                child->parent = node;
            }
            else if (token == FdtProp)
            {
                DtProp* prop = ParseProp(i);
                ASSERT(prop != nullptr, "Bad FdtBeginProp token");
                prop->next = node->props;
                node->props = prop;
                prop->node = node;
                CheckForHooks(node, prop);
            }
            else
                i++; //unknown token, ignore it
        }

        ASSERT_UNREACHABLE(); //node never terminated, we shouoldn't reach here
    }

    DtProp* DeviceTree::ParseProp(size_t& i)
    {
        if (Bs(cells[i]) != FdtProp)
            return nullptr;

        i++; //consume leading token
        ASSERT(propBuffRemain > 0, "Unable to allocate DtProp");
        DtProp* prop = propBuff;
        propBuff++;
        propBuffRemain--;

        auto fdtProp = reinterpret_cast<const FdtProperty*>(cells + i);
        const char* name = strings + Bs(fdtProp->nameOffset);
        const size_t nameLen = sl::memfirst(name, 0, stringsLength - Bs(fdtProp->nameOffset));
        ASSERT(nameLen != stringsLength - Bs(fdtProp->nameOffset), "Prop name not terminated");

        prop->name = sl::StringSpan(name, nameLen);
        prop->firstCell = cells + i + 2;
        prop->length = Bs(fdtProp->length);
        prop->node = nullptr;
        i += 2 + (sl::AlignUp(prop->length, FdtCellSize) / FdtCellSize);

        return prop;
    }

    DeviceTree globalDeviceTree;
    DeviceTree& DeviceTree::Global()
    { return globalDeviceTree; }

    void DeviceTree::Init(uintptr_t dtbAddr)
    {
        VALIDATE(dtbAddr != 0,, "Attempted to init dtb with nullptr");
        ASSERT(cells == nullptr, "Attempted to re-init device tree parser");

        auto header = reinterpret_cast<const FdtHeader*>(dtbAddr + hhdmBase);
        ASSERT(Bs(header->magic) == FdtMagic, "FDT has incorrect magic number");

        //populate pointers to the important parts of the blob
        const sl::NativePtr base (dtbAddr);
        cells = base.Offset(Bs(header->offsetStructs)).As<const uint32_t>();
        cellCount = Bs(header->sizeStructs) / FdtCellSize;
        strings = base.Offset(Bs(header->offsetStrings)).As<const char>();
        stringsLength = Bs(header->sizeStrings);
        root = nullptr;
        
        //count total nodes + properties, allocate memory to store them in.
        for (size_t i = 0; i < cellCount; i++)
        {
            if (cells[i] == FdtBeginNode)
                nodeBuffRemain++;
            else if (cells[i] == FdtProp)
                propBuffRemain++;
        }
        nodeCount = phandleCount = nodeBuffRemain;

        firstNode = nodeBuff = new DtNode[nodeBuffRemain];
        propBuff = new DtProp[propBuffRemain];
        
        //scan through fdt and construct the tree
        for (size_t i = 0; i < cellCount; i++)
        {
            if (Bs(cells[i]) != FdtBeginNode)
                continue;

            //root nodes use a default of addrCells=2 and sizeCells=1
            DtNode* node = ParseNode(i, 2, 1);
            if (node == nullptr)
                continue;
            node->sibling = root;
            root = node;
        }
    }

    bool DeviceTree::Available() const
    { return cells != nullptr; }

    DtNode* DeviceTree::FindCompatible(sl::StringSpan str, DtNode* last) const
    {
        if (!Available())
            return nullptr;

        size_t beginIndex = 0;
        if (last != nullptr)
        {
            beginIndex = reinterpret_cast<size_t>(last) - reinterpret_cast<size_t>(firstNode);
            beginIndex /= sizeof(DtNode);
            beginIndex++;
        }

        for (size_t i = beginIndex; i < nodeCount; i++)
        {
            DtProp* prop = firstNode[i].FindProp("compatible");
            if (prop == nullptr)
                continue;

            for (size_t ci = 0;; ci++)
            {
                sl::StringSpan compatStr = prop->ReadString(ci);
                if (compatStr.Empty())
                    break;
                if (compatStr == str)
                    return firstNode + i;
            }
        }

        return nullptr;
    }

    DtNode* DeviceTree::FindPHandle(size_t phandle) const
    {
        if (!Available() || phandle > phandleCount)
            return nullptr;
        return phandleLookup[phandle];
    }

    DtNode* DeviceTree::Find(sl::StringSpan path) const
    {
        if (!Available())
            return nullptr;
        (void)path;
        ASSERT_UNREACHABLE(); //TODO: implement 
    }
}
