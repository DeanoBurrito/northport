#pragma once

#include <String.h>

namespace sl
{
    enum class DemangleToken : size_t
    {
        Type_Void = 0,
        Type_Bool,
        Type_Char,
        Type_SignedChar,
        Type_UnsignedChar,
        Type_Short,
        Type_UnsignedShort,
        Type_Int,
        Type_UnsignedInt,
        Type_Long,
        Type_UnsignedLong,
        Type_LongLong,
        Type_UnsignedLongLong,
        Type_WideChar,
        Type_Float,
        Type_Double,
        Type_LongDouble,
        Type_Varargs,
        
        Qualifier_Const,
        Qualifier_Volatile,
        Qualifier_Pointer,
        Qualifier_LValueRef,
        Qualifier_RValueRef,

        ScopedName,
        ArgSeparator,

        UnscopedName,
        Ignore,
    };

    constexpr const char* tokenStrings[] = 
    {
        "void",
        "bool",
        "char",
        "signed char",
        "unsigned char",
        "short",
        "unsigned short",
        "int",
        "unsigned",
        "long",
        "unsigned long",
        "long long",
        "unsigned long long",
        "wchar_t",
        "float",
        "double",
        "long double",
        "varargs ...",

        "const",
        "volatile",
        "*",
        "&",
        "&&",

        "::",
        ", ",
    };

    constexpr const size_t tokenStringSizes[] =
    {
        4,
        4,
        4,
        11,
        13,
        5,
        14,
        3,
        8,
        4,
        13,
        9,
        18,
        7,
        5,
        6,
        11,
        11,

        5,
        8,
        1,
        1,
        2,

        2,
        2,
    };

    enum class DemangleModFlags : size_t
    {
        None = 0,
        IsConst = (1 << 0),
        IsVolatile = (1 << 1),
        IsPointer = (1 << 2),
        IsLValue = (1 << 3),
        IsRValue = (1 << 4),
    };
    
    struct DemangleNode
    {
        const DemangleToken type;
        const size_t sourceStart;
        const size_t sourceLength;
        const size_t textOffset;
        DemangleModFlags flags = DemangleModFlags::None;

        DemangleNode(DemangleToken t, size_t start, size_t len, size_t offset)
        : type(t), sourceStart(start), sourceLength(len), textOffset(offset)
        {}
    };
    
    bool IsMangledName(const string& rawName);
    string DemangleName(const string& rawName);
}
