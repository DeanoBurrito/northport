#include <elf/Demangle.h>
#include <StringCulture.h>
#include <containers/Vector.h>

namespace sl
{   
    constexpr size_t builtIndexConst = 17;
    constexpr size_t builtIndexVolatile = 18;
    constexpr size_t builtIndexPointer = 19;
    constexpr size_t builtIndexLRef = 20;
    constexpr size_t builtIndexRRef = 21;
    constexpr size_t builtIndexArgsBegin = 22;
    constexpr size_t builtIndexArgsEnd = 23;
    constexpr size_t builtIndexNameSep = 24;
    constexpr size_t builtIndexNormalSep = 25;
    constexpr size_t builtIndexDtor = 26;
    
    constexpr const char* builtInLiterals[] = 
    {
        "void", "bool", "char", "signed char",
        "unsigned char", "short", "unsigned short", "int",
        "unsigned", "long", "unsigned long", "long long",
        "unsigned long long", "wchar_t", "float", "double",

        "varargs",
        "const ", "volatile ", "*", "&", "&&",

        "(", ")", "::", ", ",
        "~",
    };

    constexpr size_t builtInLengths[] = 
    {
        4, 4, 4, 11,
        13, 5, 14, 3,
        8, 4, 13, 9,
        18, 7, 5, 6,

        7,
        6, 9, 1, 1, 2,

        1, 1, 2, 2,
        1,
    };

    void CxxDemangler::Parse()
    {
        size_t lastNodesCount = 0;
        while (inputIndex < input.Size())
        {
            size_t literalIndex = 0;
            ParseFlags();

            switch (input[inputIndex])
            {
            case 'v': literalIndex =  0; goto parse_literal;
            case 'b': literalIndex =  1; goto parse_literal;
            case 'c': literalIndex =  2; goto parse_literal;
            case 'a': literalIndex =  3; goto parse_literal;
            case 'h': literalIndex =  4; goto parse_literal;
            case 's': literalIndex =  5; goto parse_literal;
            case 't': literalIndex =  6; goto parse_literal;
            case 'i': literalIndex =  7; goto parse_literal;
            case 'j': literalIndex =  8; goto parse_literal;
            case 'l': literalIndex =  9; goto parse_literal;
            case 'm': literalIndex = 10; goto parse_literal;
            case 'x': literalIndex = 11; goto parse_literal;
            case 'y': literalIndex = 12; goto parse_literal;
            case 'w': literalIndex = 13; goto parse_literal;
            case 'f': literalIndex = 14; goto parse_literal;
            case 'd': literalIndex = 15; goto parse_literal;
            case 'z': literalIndex = 16; goto parse_literal;

            parse_literal:
            {
                nodes.EmplaceBack(literalIndex, builtInLengths[literalIndex], true);
                outputLength += builtInLengths[literalIndex];
                break;
            }

            case 'S': ParseShorthand(true); break;
            case 'N': ParseName(); break;

            default:
                if (StringCulture::current->IsDigit(input[inputIndex]))
                    ParseNameSegment(true);
            }

            if (lastNodesCount == 0)
            {
                nodes.EmplaceBack(builtIndexArgsBegin, builtInLengths[builtIndexArgsBegin], true);
                outputLength += builtInLengths[builtIndexArgsBegin];
            }
            else
            {
                nodes.EmplaceBack(builtIndexNormalSep, builtInLengths[builtIndexNormalSep], true);
                outputLength += builtInLengths[builtIndexNormalSep];
            }
            lastNodesCount = nodes.Size();

            inputIndex++;
        }

        //change trailing node from separator to args closing bracket
        DemangleNode& finalNode = nodes.Back();
        finalNode.length = builtInLengths[builtIndexArgsEnd];
        finalNode.offset = builtIndexArgsEnd;
        outputLength = outputLength - builtInLengths[builtIndexNormalSep] + builtInLengths[builtIndexArgsEnd];
    }
    
    void CxxDemangler::ParseFlags()
    {
        while (true)
        {
            switch (input[inputIndex])
            {
            case 'K': 
                currentFlags = EnumSetFlag(currentFlags, DemangleNodeFlags::Const);
                outputLength += builtInLengths[builtIndexConst]; 
                break;
            case 'V': 
                currentFlags = EnumSetFlag(currentFlags, DemangleNodeFlags::Volatile);
                outputLength += builtInLengths[builtIndexVolatile]; 
                break;
            case 'P': 
                currentFlags = EnumSetFlag(currentFlags, DemangleNodeFlags::Pointer);
                outputLength += builtInLengths[builtIndexPointer]; 
                break;
            case 'R': 
                currentFlags = EnumSetFlag(currentFlags, DemangleNodeFlags::LRef); 
                outputLength += builtInLengths[builtIndexLRef]; 
                break;
            case 'O': 
                currentFlags = EnumSetFlag(currentFlags, DemangleNodeFlags::RRef);
                outputLength += builtInLengths[builtIndexRRef]; 
                break;

            default:
                return;
            }

            inputIndex++;
        }
    }

    void CxxDemangler::ParseShorthand(bool applyFlags)
    {
        inputIndex++;
        size_t shorthandIndex = 0;
        size_t digitsLength = 0;
        if (input[inputIndex] != '_')
        {
            StringCulture::current->TryGetUInt64(&shorthandIndex, input, inputIndex);
            shorthandIndex++; //since 'S_' is index 0, and 'S0_' is actually index 1. Thanks GCC.. :/
        }
        while (StringCulture::current->IsDigit(input[inputIndex + digitsLength]))
            digitsLength++;
        
        nodes.PushBack(shorthandReferences[shorthandIndex]);
        inputIndex += digitsLength;
        outputLength += shorthandReferences[shorthandIndex].length;

        if (applyFlags)
        {
            nodes.Back().flags = currentFlags;
            currentFlags = DemangleNodeFlags::None;
        }
    }

    void CxxDemangler::ParseNameSegment(bool applyFlags)
    {
        switch (input[inputIndex])
        {
        case 'S':
            ParseShorthand(applyFlags);
            inputIndex++;
            break;

        case 'C':
        {
            //we dont care about the ctor's index, just make sure we consume the digits properly
            inputIndex++;
            size_t digitsCount = 0;
            while (StringCulture::current->IsDigit(input[inputIndex + digitsCount]))
                digitsCount++;
            inputIndex += digitsCount;

            break;
        }

        case 'D':
        {
            inputIndex++;
            size_t digitsCount = 0;
            while (StringCulture::current->IsDigit(input[inputIndex + digitsCount]))
                digitsCount++;
            inputIndex += digitsCount;

            nodes.EmplaceBack(builtIndexDtor, builtInLengths[builtIndexDtor], true);
            outputLength += builtInLengths[builtIndexDtor];
            break;
        }

        default:
            size_t nameLength = 0;
            size_t digitsLength = 0;
            StringCulture::current->TryGetUInt64(&nameLength, input, inputIndex);
            while (StringCulture::current->IsDigit(input[inputIndex + digitsLength]))
                digitsLength++;

            nodes.EmplaceBack(inputIndex + digitsLength, nameLength, false);
            shorthandReferences.PushBack(nodes.Back());

            if (applyFlags)
            {
                nodes.Back().flags = currentFlags;
                currentFlags = DemangleNodeFlags::None;
            }
            
            inputIndex += nameLength + digitsLength;
            outputLength += nameLength;
            break;
        }
    }

    void CxxDemangler::ParseName()
    {
        inputIndex++;
        bool isFirst = true;
        while (input[inputIndex] != 'E')
        {
            ParseNameSegment(false);

            if (isFirst)
            {
                isFirst = false;
                nodes.Back().flags = (DemangleNodeFlags((size_t)currentFlags & 3));
                currentFlags = EnumClearFlag(EnumClearFlag(currentFlags, DemangleNodeFlags::Volatile), DemangleNodeFlags::Const);
            }

            nodes.EmplaceBack(builtIndexNameSep, builtInLengths[builtIndexNameSep], true);
            outputLength += builtInLengths[builtIndexNameSep];
        }

        DemangleNode last = nodes.PopBack(); //remove the last separator, after the end of the name
        outputLength -= last.length;

        nodes.Back().flags = currentFlags;
        currentFlags = DemangleNodeFlags::None;
    }
    
    CxxDemangler::CxxDemangler(const string& mangled) : input(mangled)
    {
        inputIndex = 2; //since all mangled names start with "_Z"
        currentFlags = DemangleNodeFlags::None;
        outputLength = 0;
    }

    const string CxxDemangler::Demangle()
    {
        Parse();
        
        char* buffer = new char[outputLength + 1];
        size_t bufferPos = 0;

        auto RenderFlagIfNeeded = [&](const DemangleNode* node, DemangleNodeFlags flag, size_t renderIndex)
        {
            if (!EnumHasFlag(node->flags, flag))
                return;

            sl::memcopy(builtInLiterals[renderIndex], 0, buffer, bufferPos, builtInLengths[renderIndex]);
            bufferPos += builtInLengths[renderIndex];
        };

        for (size_t i = 0; i < nodes.Size(); i++)
        {
            const DemangleNode* node = &nodes[i];

            RenderFlagIfNeeded(node, DemangleNodeFlags::Const, builtIndexConst);
            RenderFlagIfNeeded(node, DemangleNodeFlags::Volatile, builtIndexVolatile);

            if (node->literalSource)
                sl::memcopy(builtInLiterals[node->offset], 0, buffer, bufferPos, node->length);
            else
                sl::memcopy(input.C_Str(), node->offset, buffer, bufferPos, node->length);
            bufferPos += node->length;

            RenderFlagIfNeeded(node, DemangleNodeFlags::Pointer, builtIndexPointer);
            RenderFlagIfNeeded(node, DemangleNodeFlags::LRef, builtIndexLRef);
            RenderFlagIfNeeded(node, DemangleNodeFlags::RRef, builtIndexRRef);
        }

        buffer[outputLength] = 0;
        return string(buffer, true);
    }

    bool IsMangledName(const string& name)
    {
        if (name.Size() < 3)
            return false;
        
        return name.BeginsWith("_Z");
    }

    const string DemangleName(const string& rawName)
    {
        if (!IsMangledName(rawName))
            return rawName;

        CxxDemangler demang(rawName);
        return demang.Demangle();
    }
}
