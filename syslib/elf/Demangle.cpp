#include <elf/Demangle.h>
#include <StringCulture.h>
#include <containers/Vector.h>

namespace sl
{   
    //rip the kernel symbol tables :(
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

    constexpr size_t builtIndexOpNew = 27;
    constexpr size_t builtIndexOpNewArray = 28;
    constexpr size_t builtIndexOpDelete = 29;
    constexpr size_t builtIndexOpDeleteArray = 30;
    constexpr size_t builtIndexOpBinaryNot = 31;
    constexpr size_t builtIndexOpPlus = 32;
    constexpr size_t builtIndexOpMinus = 33;
    constexpr size_t builtIndexOpStar = 34;
    constexpr size_t builtIndexOpDiv = 35;
    constexpr size_t builtIndexOpModulo = 36;
    constexpr size_t builtIndexOpAnd = 37;
    constexpr size_t builtIndexOpOr = 38;
    constexpr size_t builtIndexOpXor = 39;
    constexpr size_t builtIndexOpAssign = 40;
    constexpr size_t builtIndexOpPlusAssign = 41;
    constexpr size_t builtIndexOpMinusAssign = 42;
    constexpr size_t builtIndexOpStarAssign = 43;
    constexpr size_t builtIndexOpDivAssign = 44;
    constexpr size_t builtIndexOpModuloAssign = 45;
    constexpr size_t builtIndexOpAndAssign = 46;
    constexpr size_t builtIndexOpOrAssign = 47;
    constexpr size_t builtIndexOpXorAssign = 48;
    constexpr size_t builtIndexOpLShift = 49;
    constexpr size_t builtIndexOpRShift = 50;
    constexpr size_t builtIndexOpLShiftAssign = 51;
    constexpr size_t builtIndexOpRShiftAssign = 52;
    constexpr size_t builtIndexOpEquals = 53;
    constexpr size_t builtIndexOpNotEquals = 54;
    constexpr size_t builtIndexOpLessThan = 55;
    constexpr size_t builtIndexOpGreaterThan = 56;
    constexpr size_t builtIndexOpLessOrEquals = 57;
    constexpr size_t builtIndexOpGreaterOrEquals = 58;
    constexpr size_t builtIndexOpStarship = 59;
    constexpr size_t builtIndexOpNot = 60;
    constexpr size_t builtIndexOpAndAnd = 61;
    constexpr size_t builtIndexOpOrOr = 62;
    constexpr size_t builtIndexOpDeref = 63;
    constexpr size_t builtIndexOpCall = 64;
    constexpr size_t builtIndexOpIndex = 65;
    constexpr size_t builtIndexOpQuestion = 66;

    constexpr size_t builtIndexTemplateBegin = builtIndexOpLessThan;
    constexpr size_t builtIndexTemplateEnd = builtIndexOpGreaterThan;
    
    constexpr const char* builtInLiterals[] = 
    {
        "void", "bool", "char", "signed char",
        "unsigned char", "short", "unsigned short", "int",
        "unsigned", "long", "unsigned long", "long long",
        "unsigned long long", "wchar_t", "float", "double",

        "...",
        "const ", "volatile ", "*", "&", "&&",

        "(", ")", "::", ", ",
        "~",

        "new", "new[]", "delete", "delete[]",
        "~", "+", "-", "*",
        "/", "%", "&", "|", 
        "^", "=", "+=", "-=",

        "*=", "/=", "%=", "&=",
        "|=", "^=", "<<", ">>",
        "<<=", ">>=", "==", "!=",
        "<", ">", "<=", ">=",

        "<=>", "!", "&&", "||",
        "->", "()", "[]", "?"
    };

    constexpr size_t builtInLengths[] = 
    {
        4, 4, 4, 11,
        13, 5, 14, 3,
        8, 4, 13, 9,
        18, 7, 5, 6,

        3,
        6, 9, 1, 1, 2,

        1, 1, 2, 2,
        1,

        3, 5, 6, 8,
        1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1, 2, 2,

        2, 2, 2, 2,
        2, 2, 2, 2,
        3, 3, 2, 2,
        1, 1, 2, 2,

        3, 1, 2, 2,
        2, 2, 2, 1,
    };

    void CxxDemangler::Parse()
    {
        size_t lastNodesCount = 0;
        while (inputIndex < input.Size())
        {
            ParseNext();

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

    void CxxDemangler::ParseNext()
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
            nodes.Back().flags = currentFlags;
            currentFlags = DemangleNodeFlags::None;
            outputLength += builtInLengths[literalIndex];
            break;
        }

        case 'S': ParseShorthand(true); break;
        case 'N': ParseName(); break;
        case 'I': ParseTemplate(); break;

        default:
            if (StringCulture::current->IsDigit(input[inputIndex]))
                ParseNameSegment(true);
        }
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
#define IF_NEXT_IS(what, opIndex) if (input[inputIndex + 1] == what) \
{ EmplaceOp(opIndex); return true; }
#define ELIF_NEXT_IS(what, opIndex) else if (input[inputIndex + 1] == what) \
{ EmplaceOp(opIndex); return true; }
    bool CxxDemangler::ParseOperator()
    {
        if (!StringCulture::current->IsLower(input[inputIndex]))
            return false; //all operators begin with lower case text
        if (inputIndex + 1 >= input.Size())
            return false; //operators are all 2 chars long

        auto EmplaceOp = [&](size_t opIndex)
        {
            inputIndex += 2;
            outputLength += builtInLengths[opIndex];
            nodes.EmplaceBack(opIndex, builtInLengths[opIndex], true);
        };

        switch (input[inputIndex])
        {
        case 's':
            { EmplaceOp(builtIndexOpStarship); return true; }
        case 'i':
            { EmplaceOp(builtIndexOpIndex); return true; }
        case 'q':
            { EmplaceOp(builtIndexOpQuestion); return true; }
        
        case 'g':
            IF_NEXT_IS('t', builtIndexOpGreaterThan)
            ELIF_NEXT_IS('e', builtIndexOpGreaterOrEquals)
            return false;
        case 'c':
            IF_NEXT_IS('o', builtIndexOpBinaryNot)
            ELIF_NEXT_IS('l', builtIndexOpCall)
            return false;
        
        case 'e':
            IF_NEXT_IS('o', builtIndexOpXor)
            ELIF_NEXT_IS('O', builtIndexOpXorAssign)
            ELIF_NEXT_IS('q', builtIndexOpEquals)
            return false;
        case 'o':
            IF_NEXT_IS('r', builtIndexOpOr)
            ELIF_NEXT_IS('R', builtIndexOpOrAssign)
            ELIF_NEXT_IS('o', builtIndexOpOrOr)
            return false;

        case 'm':
            IF_NEXT_IS('i', builtIndexOpMinus)
            ELIF_NEXT_IS('l', builtIndexOpStar)
            ELIF_NEXT_IS('I', builtIndexOpMinusAssign)
            ELIF_NEXT_IS('L', builtIndexOpStarAssign)
            return false;
        case 'p':
            IF_NEXT_IS('s', builtIndexOpPlus)
            ELIF_NEXT_IS('l', builtIndexOpPlus)
            ELIF_NEXT_IS('L', builtIndexOpPlusAssign)
            ELIF_NEXT_IS('t', builtIndexOpDeref)
            return false;
        case 'r':
            IF_NEXT_IS('m', builtIndexOpModulo)
            ELIF_NEXT_IS('M', builtIndexOpModuloAssign)
            ELIF_NEXT_IS('s', builtIndexOpRShift)
            ELIF_NEXT_IS('S', builtIndexOpRShiftAssign)
            return false;
        case 'a':
            IF_NEXT_IS('d', builtIndexOpAnd)
            ELIF_NEXT_IS('S', builtIndexOpAssign)
            ELIF_NEXT_IS('N', builtIndexOpAndAssign)
            ELIF_NEXT_IS('a', builtIndexOpAndAnd)
            return false;
        case 'l':
            IF_NEXT_IS('s', builtIndexOpLShift)
            ELIF_NEXT_IS('S', builtIndexOpLShiftAssign)
            ELIF_NEXT_IS('t', builtIndexOpLessThan)
            ELIF_NEXT_IS('e', builtIndexOpLessOrEquals)
            return false;

        case 'n':
            IF_NEXT_IS('w', builtIndexOpNew)
            ELIF_NEXT_IS('a', builtIndexOpNewArray)
            ELIF_NEXT_IS('g', builtIndexOpMinus)
            ELIF_NEXT_IS('e', builtIndexOpNotEquals)
            ELIF_NEXT_IS('t', builtIndexOpNot)
            return false;
        case 'd':
            IF_NEXT_IS('l', builtIndexOpDelete)
            ELIF_NEXT_IS('a', builtIndexOpDeleteArray)
            ELIF_NEXT_IS('e', builtIndexOpStar)
            ELIF_NEXT_IS('v', builtIndexOpDiv)
            ELIF_NEXT_IS('V', builtIndexOpDivAssign)
            return false;
        }

        return false;
    }
#undef ELIF_NEXT_IS

    void CxxDemangler::ParseNameSegment(bool applyFlags)
    {
        ParseFlags();
        
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
            if (ParseOperator())
                break;
            
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
            if (input[inputIndex] == 'I')
            {
                if (!isFirst)
                    nodes.PopBack(); //remove the trailing separator
                ParseTemplate();
                inputIndex++;
            }
            else
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

    void CxxDemangler::ParseTemplate()
    {
        inputIndex++;
        nodes.EmplaceBack(builtIndexTemplateBegin, builtInLengths[builtIndexTemplateBegin], true);

        while (input[inputIndex] != 'E')
        {
            ParseNext();
            inputIndex++;
        }
        
        nodes.EmplaceBack(builtIndexTemplateEnd, builtInLengths[builtIndexTemplateEnd], true);
        outputLength += 2;
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

        if (outputLength < bufferPos)
            buffer[outputLength] = 0;
        else
            buffer[bufferPos] = 0;
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
