#include <Types.h>
#include <core/Log.h>

extern "C"
{
    struct SourcePos
    {
        const char* file;
        uint32_t line;
        uint32_t column;
    };

#define UBSAN_HANDLE(name, format, ...) \
    void __ubsan_handle_##name(const SourcePos* where) \
    { \
        Log("UB: " #name" @ %s:%u:%u" format, LogLevel::Error, where->file, where->line, where->column); \
    }

    UBSAN_HANDLE(load_invalid_value, "");
    UBSAN_HANDLE(nonnull_arg, "");
    UBSAN_HANDLE(nullability_arg, "");
    UBSAN_HANDLE(nonnull_return_v1, "");
    UBSAN_HANDLE(nullability_return_v1, "");
    UBSAN_HANDLE(vla_bound_not_positive, "");
    UBSAN_HANDLE(add_overflow, "");
    UBSAN_HANDLE(sub_overflow, "");
    UBSAN_HANDLE(negate_overflow, "");
    UBSAN_HANDLE(mul_overflow, "");
    UBSAN_HANDLE(shift_out_of_bounds, "");
    UBSAN_HANDLE(divrem_overflow, "");
    UBSAN_HANDLE(out_of_bounds, "");
    UBSAN_HANDLE(type_mismatch_v1, "");
    UBSAN_HANDLE(alignment_assumption, "");
    UBSAN_HANDLE(builtin_unreachable, "");
    UBSAN_HANDLE(missing_return, "");
    UBSAN_HANDLE(implicit_conversion, "");
    UBSAN_HANDLE(invalid_builtin, "");
    UBSAN_HANDLE(pointer_overflow, "");
}
