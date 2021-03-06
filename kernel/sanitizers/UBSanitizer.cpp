#include <stdint.h>
#include <Platform.h>
#include <Log.h>
#include <arch/Cpu.h>

namespace UB
{
/*
    A lot of the symbols and their interpretations were pulled from Serenity
    and thom_tl's Luna hypervisor, special thanks to them for their work.
    Abb1x's tiny ubsan also deserves a mention as the original inspiration.
*/
    using Kernel::Logf;
    using Kernel::LogSeverity;

    struct SourceLocation
    {
        const char* file;
        uint32_t line;
        uint32_t column;

        SourceLocation() : file(nullptr), line(0), column(0)
        {}

        void Print(const char* header) const
        {
            Logf("%sSource: %s line %u column %u", LogSeverity::Warning, header, file, line, column);
        }
    };

    struct TypeDescriptor
    {
        uint16_t kind;
        uint16_t info;
        char name[];

        void Print(const char* header) const
        {
            const char* typeStr;
            switch (kind)
            {
                case 0: typeStr = "Integer"; break;
                case 1: typeStr = "Float"; break;
                default: typeStr = "Unknown"; break;
            }

            Logf("%sType: %s %u-bit %s", LogSeverity::Warning, header, typeStr, 2 << info, name);
        }
    };

    struct InvalidValueData
    {
        SourceLocation where;
        const TypeDescriptor* type;
    };

    struct NonNullArgData
    {
        SourceLocation where;
        SourceLocation attributeWhere;
        int argumentIndex;
    };
    
    struct NonNullReturnData
    {
        SourceLocation where;
    };

    struct OverflowData
    {
        SourceLocation where;
        const TypeDescriptor* type;
    };

    struct VLABoundData
    {
        SourceLocation where;
        const TypeDescriptor* type;
    };

    struct ShiftOutOfBoundsData
    {
        SourceLocation where;
        const TypeDescriptor* leftOperand;
        const TypeDescriptor* rightOperand;
    };

    struct OutOfBoundsData
    {
        SourceLocation where;
        const TypeDescriptor* arrayType;
        const TypeDescriptor* indexType;
    };

    struct TypeMismatchData
    {
        SourceLocation where;
        const TypeDescriptor* type;
        uint8_t logAlignment;
        uint8_t typeCheckKind;
    };

    struct AlignmentAssumptionData
    {
        SourceLocation where;
        SourceLocation assumptionWhere;
        const TypeDescriptor* type;
    };

    struct UnreachableData
    {
        SourceLocation where;
    };

    struct ImplicitConversationData
    {
        SourceLocation where;
        const TypeDescriptor* fromType;
        const TypeDescriptor* toType;
        uint8_t kind;
    };

    struct InvalidBuiltinData
    {
        SourceLocation where;
        uint8_t kind;
    };

    struct PointerOverflowData
    {
        SourceLocation where;
    };

    struct FloatCastOverflowData
    {
        SourceLocation where;
        const TypeDescriptor* fromType;
        const TypeDescriptor* toType;
    };
}

extern "C"
{
    using namespace UB;

    [[gnu::used]]
    void __ubsan_handle_load_invalid_value(const InvalidValueData& data, uint64_t value)
    {
        data.type->Print("");
        Logf("UBSAN: load_invalid_value (value=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, value);
    }

    [[gnu::used]]
    void __ubsan_handle_nonnull_arg(const NonNullArgData& data)
    {
        Logf("UBSAN: nonnull_arg @ %s:%u,%u (arg=%u)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, data.argumentIndex);
    }

    [[gnu::used]]
    void __ubsan_handle_nullability_arg(const NonNullArgData& data)
    {
        Logf("UBSAN: nullability_arg @ %s:%u,%u (arg=%u)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, data.argumentIndex);
    }

    [[gnu::used]]
    void __ubsan_handle_nonnull_return_v1(const NonNullReturnData& data, const SourceLocation& where)
    {
        data.where.Print("");
        Logf("UBSAN: nonnull_return @ %s:%u,%u", LogSeverity::Error, where.file, where.line, where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_nullability_return_v1(const NonNullReturnData&, const SourceLocation where)
    {
        Logf("UBSAN: nullability_return_v1 @ %s:%u,%u", LogSeverity::Error, where.file, where.line, where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_vla_bound_not_positive(const VLABoundData& data, uint64_t bound)
    {
        data.type->Print("");
        Logf("UBSAN: vla_bound_not_positive @ %s:%u,%u (bound=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, bound);
    }

    [[gnu::used]]
    void __ubsan_handle_add_overflow(const OverflowData& data, uint64_t lhs, uint64_t rhs)
    {
        data.type->Print("");
        Logf("UBSAN: add_overflow @ %s:%u,%u (lhs=0x%lx, rhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    [[gnu::used]]
    void __ubsan_handle_sub_overflow(const OverflowData& data, uint64_t lhs, uint64_t rhs)
    {
        data.type->Print("");
        Logf("UBSAN: sub_overflow @ %s:%u,%u (lhs=0x%lx, rhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    [[gnu::used]]
    void __ubsan_handle_negate_overflow(const OverflowData& data, uint64_t op)
    {
        data.type->Print("");
        Logf("UBSAN: negate_overflow @ %s:%u,%u (lhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, op);
    }

    [[gnu::used]]
    void __ubsan_handle_mul_overflow(const OverflowData& data, uint64_t lhs, uint64_t rhs)
    {
        data.type->Print("");
        Logf("UBSAN: mul_overflow @ %s:%u,%u (lhs=0x%lx, rhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    [[gnu::used]]
    void __ubsan_handle_shift_out_of_bounds(const ShiftOutOfBoundsData& data, uint64_t lhs, uint64_t rhs)
    {
        data.leftOperand->Print("lhs:");
        data.leftOperand->Print("rhs:");
        Logf("UBSAN: shift_out_of_bounds @ %s:%u,%u (lhs=0x%lx, rhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    [[gnu::used]]
    void __ubsan_handle_divrem_overflow(const OverflowData& data, uint64_t lhs, uint64_t rhs)
    {
        data.type->Print("");
        Logf("UBSAN: divrem_overflow @ %s:%u,%u (lhs=0x%lx, rhs=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, lhs, rhs);
    }

    [[gnu::used]]
    void __ubsan_handle_out_of_bounds(const OutOfBoundsData& data, uint64_t operand)
    {
        data.arrayType->Print("array:");
        data.indexType->Print("index:");
        Logf("UBSAN: out_of_bounds @ %s:%u,%u (index=0x%lx)", LogSeverity::Error, data.where.file, data.where.line, data.where.column, operand);
    }

    [[gnu::used]]
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
            Logf("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s null pointer of type %s", LogSeverity::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], data.type->name);
        }
        else if (ptr != nullptr && (1 << data.logAlignment) - 1)
        {
            Logf("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s misaligned address 0x%lx of type %s", LogSeverity::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], (NativeUInt)ptr, data.type->name);
        }
        else
        {
            Logf("UBSAN: type_mismatch_v1 @ %s:%u,%u. %s address 0x%lx, not enough spce for type %s", LogSeverity::Error, 
            data.where.file, data.where.line, data.where.column, opStrings[data.typeCheckKind], (NativeUInt)ptr, data.type->name);
        }
    }

    [[gnu::used]]
    void __ubsan_handle_alignment_assumption(const AlignmentAssumptionData& data, void*, void*, void*)
    {
        Logf("UBSAN: alignment_assumption @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_builtin_unreachable(const UnreachableData& data)
    {
        Logf("UBSAN: builtin_unreachable @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_missing_return(const UnreachableData& data)
    {
        Logf("UBSAN: missing_return @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_implicit_conversion(const ImplicitConversationData& data, void*, void*)
    {
        data.fromType->Print("from:");
        data.toType->Print("  to:");
        Logf("UBSAN: implicit_conversion @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_invalid_builtin(const InvalidBuiltinData& data)
    {
        Logf("UBSAN: invalid_builtin @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }

    [[gnu::used]]
    void __ubsan_handle_pointer_overflow(const PointerOverflowData& data, void*, void*)
    {
        Logf("UBSAN: pointer_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
    }
}
