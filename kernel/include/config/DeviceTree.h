#pragma once
/* This is v3 of this device tree parser, the original version had to work in a different
 * environment, which is no longer required. If you're looking to use something like this
 * in your own project check out smoldtb (https://github.com/deanoburrito/smoldtb),
 * which provides the same functionality but it's written in C.
 *
 * If you're wondering about the excessive use of `[[gnu::always_inline]]`, it
 * provides a read-only public interface, while allowing the real variables to remain
 * mutable to certain parts of code (the device tree parser). This allows for runtime
 * modifications internally, if needed. The forced inlining of these functions 
 * compiles into a single load from memory anyway, so it's purely a C++-level thing.
 */

#include <stdint.h>
#include <stddef.h>
#include <Span.h>

namespace Npk::Config
{
    using DtValue = uintptr_t;
    using DtPair = DtValue[2];
    using DtTriplet = DtValue[3];
    using DtQuad = DtValue[4];

    struct DtNode;
    class DeviceTree;
    
    struct DtProp
    {
    friend DeviceTree;
    private:
        sl::StringSpan name;
        const uint32_t* firstCell;
        size_t length;
        DtNode* node;
        DtProp* next;

        DtProp() = default;

    public:
        [[gnu::always_inline]]
        inline sl::StringSpan Name() const
        { return name; }

        DtValue ReadValue(size_t cellCount) const;
        sl::StringSpan ReadString(size_t index) const;

        //generic read functions
        size_t ReadValues(size_t cellCount, sl::Span<DtValue> values) const;
        size_t ReadPairs(DtPair layout, sl::Span<DtPair> values) const;
        size_t ReadTriplets(DtTriplet layout, sl::Span<DtTriplet> values) const;
        size_t ReadQuads(DtQuad layout, sl::Span<DtQuad> values) const;

        //convinience functions for reading specific properties
        size_t ReadRegs(sl::Span<DtPair> values) const;
        size_t ReadRanges(sl::Span<DtTriplet> values) const;
        size_t ReadRangesWithMeta(sl::Span<DtQuad> values, size_t metadataCells) const;
    };
    
    struct DtNode
    {
    friend DeviceTree;
    private:
        DtNode* parent;
        DtNode* sibling;
        DtNode* child;
        DtProp* props;
        sl::StringSpan name;
        uint8_t addrCells;
        uint8_t sizeCells;

        DtNode() = default;

    public:
        [[gnu::always_inline]]
        inline sl::StringSpan Name() const
        { return name; }

        [[gnu::always_inline]]
        inline DtNode* Parent() const
        { return parent; }

        [[gnu::always_inline]]
        inline DtNode* Child() const
        { return child; }

        [[gnu::always_inline]]
        inline DtNode* Sibling() const
        { return sibling; }

        [[gnu::always_inline]]
        inline size_t AddrCells() const
        { return addrCells; }

        [[gnu::always_inline]]
        inline size_t SizeCells() const
        { return sizeCells; }

        DtNode* FindChild(sl::StringSpan name) const;
        DtProp* FindProp(sl::StringSpan name) const;
    };

    class DeviceTree
    {
    private:
        const uint32_t* cells = nullptr;
        size_t cellCount;
        const char* strings;
        size_t stringsLength;

        DtNode* root;
        DtNode** phandleLookup;
        size_t phandleCount;
        DtNode* firstNode;
        size_t nodeCount;

        DtNode* nodeBuff;
        size_t nodeBuffRemain;
        DtProp* propBuff;
        size_t propBuffRemain;

        void CheckForHooks(DtNode* node, DtProp* prop);
        DtNode* ParseNode(size_t& i, uint8_t addrCells, uint8_t sizeCells);
        DtProp* ParseProp(size_t& i);
    public:
        static DeviceTree& Global();
        
        void Init(uintptr_t dtbAddr);
        bool Available() const;

        DtNode* FindCompatible(sl::StringSpan str, DtNode* last = nullptr) const;
        DtNode* FindPHandle(size_t phandle) const;
        DtNode* Find(sl::StringSpan path) const;
    };
}
