#pragma once

#include <String.h>
#include <containers/Vector.h>

namespace sl
{
    class CxxDemangler
    {
    private:
        enum class DemangleNodeFlags : uint8_t
        {
            None = 0,
            Const = 1 << 0,
            Volatile = 1 << 1,
            Pointer = 1 << 2,
            LRef = 1 << 3,
            RRef = 1 << 4,
        };

        struct DemangleNode
        {
            size_t offset;
            size_t length;

            bool literalSource; //if false, input source
            DemangleNodeFlags flags;

            DemangleNode(size_t off, size_t len, bool isLit) : offset(off), length(len), literalSource(isLit), flags(DemangleNodeFlags::None)
            {}
        };

        const string& input;
        sl::Vector<DemangleNode> nodes;
        sl::Vector<DemangleNode> shorthandReferences;
        size_t inputIndex;
        size_t outputLength;
        DemangleNodeFlags currentFlags;

        void Parse();
        void ParseFlags();
        void ParseShorthand(bool applyFlags);
        void ParseNameSegment(bool applyFlags);
        void ParseName();

    public:
        CxxDemangler(const string& mangled);

        const string Demangle();
    };
    
    bool IsMangledName(const string& rawName);
    const string DemangleName(const string& rawName);
}
