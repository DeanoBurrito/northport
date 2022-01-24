#include <stdint.h>
#include <Platform.h>
#include <Log.h>
#include <Cpu.h>

namespace UB
{
    struct SourceLocation
    {
        const char* file;
        uint32_t line;
        uint32_t column;

        SourceLocation() : file(nullptr), line(0), column(0)
        {}
    };

    struct TypeDescriptor
    {
        uint16_t kind;
        uint16_t info;
        char name[];

        FORCE_INLINE bool IsInt() const
        { return kind == 0; }
        FORCE_INLINE bool IsFloat() const
        { return kind == 1; }
        FORCE_INLINE bool IsSigned() const
        { return info & 1; }
        FORCE_INLINE bool IsUnsigned() const
        { return !IsSigned(); }
        FORCE_INLINE size_t BitWidth() const
        { return 1 << (info >> 1); }
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

// #define INDEF_HALT while(1) CPU::Halt();
#define INDEF_HALT

//these symbols are pulled from serenity OS and gcc/clang error logs when compiling.
extern "C"
{
    using namespace UB;
    using Kernel::Logf;
    using Kernel::LogSeverity;
    using Kernel::CPU;

    [[gnu::used]]
    void __ubsan_handle_load_invalid_value(const InvalidValueData& data, void*)
    {
        Logf("UBSAN: load_invalid_value @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_nonnull_arg(const NonNullArgData& data)
    {
        Logf("UBSAN: nonnull_arg @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_nullability_arg(const NonNullArgData& data)
    {
        Logf("UBSAN: nullability_arg @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_nonnull_return_v1(const NonNullReturnData&, const SourceLocation& where)
    {
        Logf("UBSAN: nonnull_return @ %s:%u,%u", LogSeverity::Error, where.file, where.line, where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_nullability_return_v1(const NonNullReturnData&, const SourceLocation where)
    {
        Logf("UBSAN: nullability_return_v1 @ %s:%u,%u", LogSeverity::Error, where.file, where.line, where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_vla_bound_not_positive(const VLABoundData& data, void*)
    {
        Logf("UBSAN: vla_bound_not_positive @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_add_overflow(const OverflowData& data, void*, void*)
    {
        Logf("UBSAN: add_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_sub_overflow(const OverflowData& data, void*, void*)
    {
        Logf("UBSAN: sub_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_negate_overflow(const OverflowData& data, void*)
    {
        Logf("UBSAN: negate_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_mul_overflow(const OverflowData& data, void*, void*)
    {
        Logf("UBSAN: mul_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_shift_out_of_bounds(const ShiftOutOfBoundsData& data, void*, void*)
    {
        Logf("UBSAN: shift_out_of_bounds @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_divrem_overflow(const OverflowData& data, void*, void*)
    {
        Logf("UBSAN: divrem_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_out_of_bounds(const OutOfBoundsData& data, void*)
    {
        Logf("UBSAN: out_of_bounds @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_type_mismatch_v1(const TypeMismatchData& data, void*)
    {
#ifdef NORTHPORT_SUPPRESS_UBSAN_TYPE_MISMATCH
        return;
#endif
        
        Logf("UBSAN: type_mismatch_v1 @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_alignment_assumption(const AlignmentAssumptionData& data, void*, void*, void*)
    {
        Logf("UBSAN: alignment_assumption @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_builtin_unreachable(const UnreachableData& data)
    {
        Logf("UBSAN: builtin_unreachable @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_missing_return(const UnreachableData& data)
    {
        Logf("UBSAN: missing_return @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_implicit_conversion(const ImplicitConversationData& data, void*, void*)
    {
        Logf("UBSAN: implicit_conversion @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_invalid_builtin(const InvalidBuiltinData& data)
    {
        Logf("UBSAN: invalid_builtin @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }

    [[gnu::used]]
    void __ubsan_handle_pointer_overflow(const PointerOverflowData& data, void*, void*)
    {
        Logf("UBSAN: pointer_overflow @ %s:%u,%u", LogSeverity::Error, data.where.file, data.where.line, data.where.column);
        INDEF_HALT;
    }
}
