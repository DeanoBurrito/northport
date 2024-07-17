#include <stdint.h>
#include <debug/Log.h>
#include <interfaces/Helpers.h>

#pragma GCC diagnostic ignored "-Wformat"

extern "C"
{
    struct SourcePos
    {
        const char* file;
        uint32_t line;
        uint32_t column;
    };

    struct TypeDescriptor
    {
        uint16_t kind;
        uint16_t bits;
        char name[];
    };

    struct InvalidValueData
    {
        SourcePos where;
        const TypeDescriptor* type;
    };

    struct NonNullArgData
    {
        SourcePos where;
        SourcePos attribWhere;
        int argumentIndex;
    };

    struct NonNullReturnData
    {
        SourcePos where;
    };

    struct OverflowData
    {
        SourcePos where;
        const TypeDescriptor* type;
    };

    struct VLABoundData
    {
        SourcePos where;
        const TypeDescriptor* type;
    };

    struct ShiftOutOfBoundsData
    {
        SourcePos where;
        const TypeDescriptor* leftOperand;
        const TypeDescriptor* rightOperand;
    };

    struct OutOfBoundsData
    {
        SourcePos where;
        const TypeDescriptor* arrayType;
        const TypeDescriptor* indexType;
    };

    struct TypeMismatchData
    {
        SourcePos where;
        const TypeDescriptor* type;
        uint8_t logAlignment;
        uint8_t typeCheckKind;
    };

    struct AlignmentAssumptionData
    {
        SourcePos where;
        SourcePos assumptionWhere;
        const TypeDescriptor* type;
    };

    struct UnreachableData
    {
        SourcePos where;
    };

    struct ImplicitConversationData
    {
        SourcePos where;
        const TypeDescriptor* fromType;
        const TypeDescriptor* toType;
        uint8_t kind;
    };

    struct InvalidBuiltinData
    {
        SourcePos where;
        uint8_t kind;
    };

    struct PointerOverflowData
    {
        SourcePos where;
    };

    struct FloatCastOverflowData
    {
        SourcePos where;
        const TypeDescriptor* fromType;
        const TypeDescriptor* toType;
    };

    void PrintType(const char* prefix, const TypeDescriptor* desc)
    {
        const char* typeStr;
        switch (desc->kind)
        {
        case 0: typeStr = "integer"; break;
        case 1: typeStr = "float"; break;
        default: typeStr = "unknown"; break;
        }

        Log("%s: %u-bit %s %s", LogLevel::Warning, prefix, 2 << desc->bits, typeStr, desc->name);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_load_invalid_value(const InvalidValueData& data, uintmax_t value)
    {
        PrintType("type", data.type);
        Log("UBSAN: load_invalid_value @ %s:%u:%u(value=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, value);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_nonnull_arg(const NonNullArgData& data)
    {
        Log("UBSAN: nonnull_arg @ %s:%u,%u (argIndex=%u)", LogLevel::Error, data.where.file, data.where.line, data.where.column, data.argumentIndex);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_nullability_arg(const NonNullArgData& data)
    {
        Log("UBSAN: nullability_arg @ %s:%u,%u (arg=%u)", LogLevel::Error, data.where.file, data.where.line, data.where.column, data.argumentIndex);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_nonnull_return_v1(const NonNullReturnData& data, const SourcePos& where)
    {
        (void)data;
        Log("UBSAN: nonnull_return @ %s:%u,%u", LogLevel::Error, where.file, where.line, where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_nullability_return_v1(const NonNullReturnData&, const SourcePos& where)
    {
        Log("UBSAN: nullability_return_v1 @ %s:%u,%u", LogLevel::Error, where.file, where.line, where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_vla_bound_not_positive(const VLABoundData& data, uintmax_t bound)
    {
        PrintType("VLA type", data.type);
        Log("UBSAN: vla_bound_not_positive @ %s:%u,%u (bound=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, bound);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_add_overflow(const OverflowData& data, uintmax_t lhs, uintmax_t rhs)
    {
        PrintType("type", data.type);
        Log("UBSAN: add_overflow @ %s:%u,%u (lhs=0x%jx, rhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_sub_overflow(const OverflowData& data, uintmax_t lhs, uintmax_t rhs)
    {
        PrintType("type", data.type);
        Log("UBSAN: sub_overflow @ %s:%u,%u (lhs=0x%jx, rhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_negate_overflow(const OverflowData& data, uintmax_t op)
    {
        PrintType("type", data.type);
        Log("UBSAN: negate_overflow @ %s:%u,%u (lhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, op);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_mul_overflow(const OverflowData& data, uintmax_t lhs, uintmax_t rhs)
    {
        PrintType("type", data.type);
        Log("UBSAN: mul_overflow @ %s:%u,%u (lhs=0x%jx, rhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_shift_out_of_bounds(const ShiftOutOfBoundsData& data, uintmax_t lhs, uintmax_t rhs)
    {
        PrintType("left op", data.leftOperand);
        PrintType("right op", data.rightOperand);
        Log("UBSAN: shift_out_of_bounds @ %s:%u,%u (lhs=0x%jx, rhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_divrem_overflow(const OverflowData& data, uintmax_t lhs, uintmax_t rhs)
    {
        PrintType("type", data.type);
        Log("UBSAN: divrem_overflow @ %s:%u,%u (lhs=0x%jx, rhs=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_out_of_bounds(const OutOfBoundsData& data, uintmax_t operand)
    {
        PrintType("array type", data.arrayType);
        PrintType("index type", data.indexType);
        Log("UBSAN: out_of_bounds @ %s:%u,%u (index=0x%jx)", LogLevel::Error, data.where.file, data.where.line, data.where.column, operand);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_type_mismatch_v1(const TypeMismatchData& data, void* ptr)
    {
        constexpr const char* opStrings[] = 
        {
            "load of",
            "store to",
            "reference binding to",
            "member access within",
            "member call on",
            "constructor call on",
            "downcast of",
            "downcast of",
            "upcast of",
            "cast to virtual base of",
            "nonnull binding to",
            "dynamic operation on"
        };
        
        if (ptr == nullptr)
        {
            Log("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s null pointer of type %s", LogLevel::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], data.type->name);
        }
        else if (ptr != nullptr && (1 << data.logAlignment) - 1)
        {
            Log("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s misaligned address %p of type %s", LogLevel::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], ptr, data.type->name);
        }
        else
        {
            Log("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s address %p, not enough spce for type %s", LogLevel::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], ptr, data.type->name);
        }
    }

    DRIVER_API_FUNC
    void __ubsan_handle_alignment_assumption(const AlignmentAssumptionData& data, void*, void*, void*)
    {
        Log("UBSAN: alignment_assumption @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_builtin_unreachable(const UnreachableData& data)
    {
        Log("UBSAN: builtin_unreachable @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_missing_return(const UnreachableData& data)
    {
        Log("UBSAN: missing_return @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_implicit_conversion(const ImplicitConversationData& data, void*, void*)
    {
        PrintType("from", data.fromType);
        PrintType("to", data.toType);
        Log("UBSAN: implicit_conversion @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_invalid_builtin(const InvalidBuiltinData& data)
    {
        Log("UBSAN: invalid_builtin @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }

    DRIVER_API_FUNC
    void __ubsan_handle_pointer_overflow(const PointerOverflowData& data, void*, void*)
    {
        Log("UBSAN: pointer_overflow @ %s:%u,%u", LogLevel::Error, data.where.file, data.where.line, data.where.column);
    }
}
