#pragma once

#include <Types.h>
#include <Compiler.h>

namespace Npk
{
#ifdef NPK_HAS_KASAN
    void InitAsan();
    void AsanPoison(uintptr_t where, size_t length);
    void AsanUnpoison(uintptr_t where, size_t length);
    void AsanClean(uintptr_t where, size_t length);
#else
    SL_ALWAYS_INLINE
    void InitAsan()
    { } //no-op

    SL_ALWAYS_INLINE
    void AsanPoison(uintptr_t where, size_t length)
    { (void)where; (void)length; } //no-op

    SL_ALWAYS_INLINE
    void AsanUnpoison(uintptr_t where, size_t length)
    { (void)where; (void)length; } //no-op

    SL_ALWAYS_INLINE
    void AsanClean(uintptr_t where, size_t length)
    { (void)where; (void)length; } //no-op
#endif
}
