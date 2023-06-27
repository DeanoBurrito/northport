#pragma once

#include <stdint.h>
#include <Optional.h>

namespace Npk::Config
{
    struct DtPair
    {
        union { uintptr_t base; uintptr_t a; };
        union { uintptr_t length; uintptr_t b; };
    };

    struct DtTriplet
    {
        union { uintptr_t parentBase; uintptr_t a; };
        union { uintptr_t childBase; uintptr_t b; };
        union { uintptr_t length; uintptr_t c; };
        
        union { uint32_t metadata; }; //yes, I know this is non longer 3 values.
    };
    
    using DtReg = DtPair;
    using DtRange = DtTriplet;
    using DtbPtr = size_t;

    struct DtNode;
    
    struct DtProperty
    {
        DtbPtr ptr;
        const char* name;

        const char* ReadStr(size_t index = 0) const;
        uint32_t ReadNumber() const;
        size_t ReadRegs(const DtNode& node, DtReg* regs) const;
        size_t ReadPairs(size_t aCells, size_t bCells, DtPair* pairs) const;
        size_t ReadRanges(const DtNode& node, DtRange* ranges) const;

    };

    struct DtNode
    {
        constexpr DtNode() : ptr(0), parentPtr(0), childPtr(0), propPtr(0), 
        propCount(0), childCount(0), addrCells(0), sizeCells(0), childAddrCells(0), 
        childSizeCells(0), name(nullptr)
        {}
        
        DtbPtr ptr;
        DtbPtr parentPtr;
        DtbPtr childPtr;
        DtbPtr propPtr;
        uint16_t propCount;
        uint16_t childCount;
        uint8_t addrCells;
        uint8_t sizeCells;
        uint8_t childAddrCells;
        uint8_t childSizeCells;
        const char* name;

        sl::Opt<const DtNode> GetChild(const char* str) const;
        sl::Opt<const DtNode> GetChild(size_t index) const;
        sl::Opt<const DtProperty> GetProp(size_t index) const;
        sl::Opt<const DtProperty> GetProp(const char* name) const;
    };

    class DeviceTree
    {
    private:
        const uint32_t* cells;
        size_t cellsCount; //used to store init state, if this is non-zero, its been initialized.
        const uint8_t* strings;
        DtNode rootNode;

        sl::Opt<const DtNode> FindCompatHelper(const DtNode& scan, const char* str, size_t strlen, DtbPtr start) const;
        sl::Opt<const DtNode> FindHandleHelper(const DtNode& scan, size_t handle) const;
        size_t SkipNode(size_t start) const;
        DtNode CreateNode(size_t start, size_t addrCells, size_t sizeCells) const;
        DtProperty CreateProperty(size_t start) const;

    public:
        constexpr DeviceTree() : cells(nullptr), cellsCount(0), strings(nullptr),
        rootNode()
        {}

        static DeviceTree& Global();
        
        void Init(uintptr_t dtbAddr);
        bool Available();

        sl::Opt<const DtNode> GetNode(const char* path) const;
        sl::Opt<const DtNode> GetCompatibleNode(const char* compatStr, sl::Opt<const DtNode> start = {}) const;
        sl::Opt<const DtNode> GetByPHandle(size_t handle) const;
        sl::Opt<const DtNode> GetChild(const DtNode& parent, const char* name) const;
        sl::Opt<const DtNode> GetChild(const DtNode& parent, size_t index) const;
        sl::Opt<const DtProperty> GetProp(const DtNode& node, const char*) const;
        sl::Opt<const DtProperty> GetProp(const DtNode& node, size_t index) const;

        const char* ReadStr(const DtProperty& prop, size_t index = 0) const;
        uint32_t ReadNumber(const DtProperty& prop) const;
        size_t ReadRegs(const DtNode& node, const DtProperty& prop, DtReg* regs) const;
        size_t ReadPairs(const DtProperty& prop, size_t aCells, size_t bCells, DtPair* pairs) const;
        size_t ReadRanges(const DtNode& node, const DtProperty& prop, DtRange* ranges) const;
    };

    //effectively these are just type-safe macros.

    [[gnu::always_inline]]
    inline const char* DtProperty::ReadStr(size_t index) const
    { return DeviceTree::Global().ReadStr(*this, index); }

    [[gnu::always_inline]]
    inline uint32_t DtProperty::ReadNumber() const
    { return DeviceTree::Global().ReadNumber(*this); }

    [[gnu::always_inline]]
    inline size_t DtProperty::ReadRegs(const DtNode& node, DtReg* regs) const
    { return DeviceTree::Global().ReadRegs(node, *this, regs); }

    [[gnu::always_inline]]
    inline size_t DtProperty::ReadPairs(size_t aCells, size_t bCells, DtPair* pairs) const
    { return DeviceTree::Global().ReadPairs(*this, aCells, bCells, pairs); }

    [[gnu::always_inline]]
    inline size_t DtProperty::ReadRanges(const DtNode& node, DtRange* ranges) const
    { return DeviceTree::Global().ReadRanges(node, *this, ranges); }

    [[gnu::always_inline]]
    inline sl::Opt<const DtNode> DtNode::GetChild(const char* name) const
    { return DeviceTree::Global().GetChild(*this, name); } 
    
    [[gnu::always_inline]]
    inline sl::Opt<const DtNode> DtNode::GetChild(size_t index) const
    { return DeviceTree::Global().GetChild(*this, index); }

    [[gnu::always_inline]]
    inline sl::Opt<const DtProperty> DtNode::GetProp(size_t index) const
    { return DeviceTree::Global().GetProp(*this, index); }

    [[gnu::always_inline]]
    inline sl::Opt<const DtProperty> DtNode::GetProp(const char* name) const
    { return DeviceTree::Global().GetProp(*this, name); }
}
