#include <elf/Demangle.h>
#include <containers/Vector.h>
#include <StringCulture.h>

namespace sl
{    
    bool IsMangledName(const string& name)
    {
        if (name.Size() < 3)
            return false;
        
        return name.BeginsWith("_Z");
    }

    size_t ParseMangledName(sl::Vector<DemangleNode>& nodes, sl::Vector<DemangleNode>& shorthandRefs, const string& input)
    {
        size_t outputLength = 0;

        DemangleToken nextTokenType = DemangleToken::Ignore;
        bool nextIsConst, nextIsVolatile, nextIsPointer, nextIsLValue, nextIsRValue;

        auto ResetTypeFlags = [&]()
        {
            nextIsConst = nextIsVolatile = nextIsPointer = nextIsLValue = nextIsRValue = false;
        };

        auto ParseShorthand = [&](size_t& i)
        {
            i++; //consume leading 'S'
            size_t index = 0;
            StringCulture::current->TryGetUInt64(&index, input, i);
            size_t digitsCount = 0;
            while (StringCulture::current->IsDigit(input[i + digitsCount]))
                digitsCount++;
            nodes.PushBack(shorthandRefs[index]);
            
            i += digitsCount;
            outputLength += shorthandRefs[index].sourceLength - shorthandRefs[index].textOffset;
        };

        auto ApplyFlags = [&](DemangleNode& node)
        {
            node.flags = sl::EnumSetFlagState(node.flags, DemangleModFlags::IsConst, nextIsConst);
            node.flags = sl::EnumSetFlagState(node.flags, DemangleModFlags::IsVolatile, nextIsVolatile);
            node.flags = sl::EnumSetFlagState(node.flags, DemangleModFlags::IsPointer, nextIsPointer);
            node.flags = sl::EnumSetFlagState(node.flags, DemangleModFlags::IsLValue, nextIsLValue);
            node.flags = sl::EnumSetFlagState(node.flags, DemangleModFlags::IsRValue, nextIsRValue);
        };

        ResetTypeFlags();
        //start at 2, since the first 2 chars will be '_Z'
        for (size_t i = 2; i < input.Size(); i++)
        {
            if (StringCulture::current->IsDigit(input[i]))
            {
                //input is an unscoped name
                size_t count = 0;
                StringCulture::current->TryGetUInt64(&count, input, i);
                size_t textOffset = 1;
                while (StringCulture::current->IsDigit(input[i + textOffset]))
                    textOffset++;

                DemangleNode node(DemangleToken::UnscopedName, i, count + textOffset, textOffset);
                ApplyFlags(node);
                nodes.PushBack(node);
                shorthandRefs.PushBack(node);

                outputLength += count;
                i += count + textOffset - 1; //-1 because there is not tailing 'E' to consume, like a scoped name.
                continue;
            }
            
            switch (input[i])
            {
            case 'v':
                nextTokenType = DemangleToken::Type_Void;
                break;
            case 'b':
                nextTokenType = DemangleToken::Type_Bool;
                break;
            case 'c':
                nextTokenType = DemangleToken::Type_Char;
                break;
            case 'a':
                nextTokenType = DemangleToken::Type_SignedChar;
                break;
            case 'h':
                nextTokenType = DemangleToken::Type_UnsignedChar;
                break;
            case 's':
                nextTokenType = DemangleToken::Type_Short;
                break;
            case 't':
                nextTokenType = DemangleToken::Type_UnsignedShort;
                break;
            case 'i':
                nextTokenType = DemangleToken::Type_Int;
                break;
            case 'j':
                nextTokenType = DemangleToken::Type_UnsignedInt;
                break;
            case 'l':
                nextTokenType = DemangleToken::Type_Long;
                break;
            case 'm':
                nextTokenType = DemangleToken::Type_UnsignedLong;
                break;
            case 'x':
                nextTokenType = DemangleToken::Type_LongLong;
                break;
            case 'y':
                nextTokenType = DemangleToken::Type_UnsignedLongLong;
                break;
            case 'w':
                nextTokenType = DemangleToken::Type_WideChar;
                break;
            case 'f':
                nextTokenType = DemangleToken::Type_Float;
                break;
            case 'd':
                nextTokenType = DemangleToken::Type_Double;
                break;
            case 'e':
                nextTokenType = DemangleToken::Type_LongDouble;
                break;
            case 'z':
                nextTokenType = DemangleToken::Type_Varargs;
                break;

            case 'K':
                nextIsConst = true;
                outputLength += tokenStringSizes[(size_t)DemangleToken::Qualifier_Const] + 1;
                break;
            case 'V':
                nextIsVolatile = true;
                outputLength += tokenStringSizes[(size_t)DemangleToken::Qualifier_Volatile] + 1;
                break;
            case 'P':
                nextIsPointer = true;
                outputLength += tokenStringSizes[(size_t)DemangleToken::Qualifier_Pointer];
                break;
            case 'R':
                nextIsLValue = true;
                outputLength += tokenStringSizes[(size_t)DemangleToken::Qualifier_LValueRef];
                break;
            case 'O':
                nextIsRValue = true;
                outputLength += tokenStringSizes[(size_t)DemangleToken::Qualifier_RValueRef] + 1;
                break;

            case 'N':
            {
                /*  Name Entry:
                    These are encoded are a series of length-prefixed strings. For example 'N6Kernel' means there is a name (N),
                    that is 6 chars long, and the name itself is 'Kernel'. A number of names can be encoded after the N, until and 'E' is reached.
                    Each name is its own token, and can be referenced separately in shorthand.
                */
                i++; //consume leading 'N'
                while (input[i] != 'E')
                {
                    if (input[i] == 'S')
                    {
                        ParseShorthand(i);
                        i++; //consume trailing '_'
                        continue;
                    }
                    if (input[i] == 'E')
                        break;
                    
                    size_t count = 0;
                    StringCulture::current->TryGetUInt64(&count, input, i);
                    size_t textOffset = 1;
                    while (StringCulture::current->IsDigit(input[i + textOffset]))
                        textOffset++;
                    
                    DemangleNode node(DemangleToken::ScopedName, i, count + textOffset, textOffset);
                    ApplyFlags(node);
                    nodes.PushBack(node);
                    shorthandRefs.PushBack(node);

                    outputLength += count + tokenStringSizes[(size_t)DemangleToken::ScopedName];
                    i += count + textOffset;
                }
                //trailing 'E' will be consumed by post loop statement (i++)
                break;
            }

            case 'S':
                ParseShorthand(i);
                break;

            default:
                break;
            }

            if (nextTokenType == DemangleToken::Ignore)
                continue;
            
            DemangleNode node(nextTokenType, i, 1, 0);
            ApplyFlags(node);
            nodes.PushBack(node);
            ResetTypeFlags();
            nextTokenType = DemangleToken::Ignore;
        }

        //if its a function, we'll need room to display open/close brackets
        outputLength += 2;

        return outputLength;
    }

    string PrintDemangleTokens(sl::Vector<DemangleNode>& nodes, sl::Vector<DemangleNode>& shorthandRefs, const string& input, size_t outputLength)
    {
        char* buffer = new char[outputLength + 1];
        size_t outputPos = 0;

        bool enteredArgBrackets = false;
        DemangleNode separatorNode(DemangleToken::ArgSeparator, 0, 2, 0);

        auto PrintToken = [&](DemangleToken token, bool spaceSuffix)
        {
            sl::memcopy(tokenStrings[(size_t)token], 0, buffer, outputPos, tokenStringSizes[(size_t)token]);
            outputPos += tokenStringSizes[(size_t)token];

            if (spaceSuffix)
            {
                buffer[outputPos] = ' ';
                outputPos++;
            }
        };

        auto PrintSimpleToken = [&](DemangleNode* node)
        {
            if (sl::EnumHasFlag(node->flags, DemangleModFlags::IsConst))
                PrintToken(DemangleToken::Qualifier_Const, true);
            if (sl::EnumHasFlag(node->flags, DemangleModFlags::IsVolatile))
                PrintToken(DemangleToken::Qualifier_Volatile, true);

            sl::memcopy(tokenStrings[(size_t)node->type], 0, buffer, outputPos, tokenStringSizes[(size_t)node->type]);
            outputPos += tokenStringSizes[(size_t)node->type];
            
            if (sl::EnumHasFlag(node->flags, DemangleModFlags::IsPointer))
                PrintToken(DemangleToken::Qualifier_Pointer, false);
            if (sl::EnumHasFlag(node->flags, DemangleModFlags::IsLValue))
                PrintToken(DemangleToken::Qualifier_LValueRef, false);
            if (sl::EnumHasFlag(node->flags, DemangleModFlags::IsRValue))
                PrintToken(DemangleToken::Qualifier_RValueRef, false);
        };

        auto PrintName = [&](DemangleNode* node, bool includePrefix)
        {
            if (includePrefix)
                PrintSimpleToken(node); //potentially dangerous if we pass an unscoped name, not a scoped one.
            
            sl::memcopy(input.C_Str(), node->sourceStart + node->textOffset, buffer, outputPos, node->sourceLength - node->textOffset);
            outputPos += node->sourceLength - node->textOffset;
        };

        for (auto it = nodes.Begin(); it < nodes.End(); ++it)
        {
            if (it->type == DemangleToken::Ignore)
                continue;
            
            switch (it->type)
            {
            case DemangleToken::ScopedName:
                {
                    bool needsPrefix = false;
                    while (it->type == DemangleToken::ScopedName)
                    {
                        PrintName(it, needsPrefix);
                        needsPrefix = true;
                        ++it;
                    }
                    --it;
                }
                if (it + 1 != nodes.End() && !enteredArgBrackets)
                {
                    //there are types following the name, must be a function-like symbol
                    enteredArgBrackets = true;
                    buffer[outputPos] = '(';
                    outputPos++;
                }
                break;

            case DemangleToken::UnscopedName:
                PrintName(it, false);
                if (it + 1 != nodes.End() && !enteredArgBrackets)
                {
                    enteredArgBrackets = true;
                    buffer[outputPos] = '(';
                    outputPos++;
                }
                break;

            default:
                PrintSimpleToken(it);
                break;
            }

            if (enteredArgBrackets && buffer[outputPos - 1] != '(' && (it + 1) != nodes.End())
            {
                PrintSimpleToken(&separatorNode);
            }
        }

        if (enteredArgBrackets)
        {
            buffer[outputPos] = ')';
            outputPos++;
        }
        
        buffer[outputPos] = 0;
        return string(buffer, true); //use buffer-owning ctor of string
    }

    string DemangleName(const string& rawName)
    {
        if (!IsMangledName(rawName))
            return rawName;
        
        sl::Vector<DemangleNode> nodes;
        sl::Vector<DemangleNode> shorthandRefs;

        size_t outputLength = ParseMangledName(nodes, shorthandRefs, rawName);
        return PrintDemangleTokens(nodes, shorthandRefs, rawName, outputLength);
    }
}
