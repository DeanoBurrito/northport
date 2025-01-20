#include <Types.h>
#include <Compiler.h>
#include <cpp/Asan.h>
#include <core/Log.h>
#include <Memory.h>

namespace Npk
{
    constexpr uintptr_t AsanBase = 0xFFFF'D000'0000'0000;
    constexpr uintptr_t AsanDelta = 0xDFFF'E000'0000'0000;
    constexpr size_t AsanShadowShift = 3;
    constexpr size_t AsanShadowScale = 1 << AsanShadowShift;
    constexpr size_t AsanShadowMask = AsanShadowScale - 1;
    constexpr size_t AsanShadowLength = 1000'0000'0000;

    sl::Span<uint8_t> asanStore;

    inline int8_t* ShadowOf(uintptr_t addr)
    {
        return reinterpret_cast<int8_t*>(AsanDelta + (addr >> AsanShadowShift));
    }

    inline uintptr_t PointerOf(int8_t* shadow)
    {
        return (reinterpret_cast<uintptr_t>(shadow) - AsanDelta) << AsanShadowShift;
    }

    SL_NO_KASAN
    void InitAsan()
    {
    }

    SL_NO_KASAN
    void AsanPoison(uintptr_t where, size_t length)
    {
        auto shadow = ShadowOf(where);
        for (size_t i = 0; i < (length >> AsanShadowShift); i++)
        {
            ASSERT_(shadow[i] == static_cast<int8_t>(0));
            shadow[i] = 0xFF;
        }
        if ((length & AsanShadowMask) != 0)
        {
            ASSERT_(shadow[length >> AsanShadowShift] == (length & AsanShadowMask));
            shadow[length >> AsanShadowShift] = 0xFF;
        }
    }

    SL_NO_KASAN
    void AsanUnpoison(uintptr_t where, size_t length)
    {
        auto shadow = ShadowOf(where);
        for (size_t i = 0; i < (length >> AsanShadowShift); i++)
        {
            ASSERT_(shadow[i] == static_cast<int8_t>(0xFF));
            shadow[i] = 0;
        }
        if ((length & AsanShadowMask) != 0)
        {
            ASSERT_(shadow[length >> AsanShadowShift] == static_cast<int8_t>(0xFF));
            shadow[length >> AsanShadowShift] = length & AsanShadowMask;
        }
    }
    
    SL_NO_KASAN
    void AsanClean(uintptr_t where, size_t length)
    {
        auto shadow = ShadowOf(where);
        for (size_t i = 0; i < (length >> AsanShadowShift); i++)
            shadow[i] = 0xFF;
        if ((length & AsanShadowMask) != 0)
            shadow[length >> AsanShadowShift] = 0xFF;
    }

    SL_NO_KASAN
    static void AsanReport(uintptr_t where, size_t size, bool write, void* pc)
    {
        Log("kasan violation: %s of %zu bytes at 0x%tx, pc=%p", LogLevel::Fatal,
            write ? "write" : "read", size, where, pc);
    }

    SL_NO_KASAN
    static void AsanAccess(uintptr_t where, size_t size, bool write, void* pc)
    {
        //TODO: GCC interface, we need to check the shadow space ourself, we also cant use Log() here, use Panic() directly
        Log("kasan violation: %s of %zu bytes at 0x%tx, pc=%p", LogLevel::Fatal,
            write ? "write" : "read", size, where, pc);
    }
}

extern "C"
{
    using namespace Npk;

    void __asan_report_load1_noabort(uintptr_t addr)
    { AsanReport(addr, 1, false, __builtin_return_address(0)); }

    void __asan_report_load2_noabort(uintptr_t addr)
    { AsanReport(addr, 2, false, __builtin_return_address(0)); }

    void __asan_report_load4_noabort(uintptr_t addr)
    { AsanReport(addr, 4, false, __builtin_return_address(0)); }

    void __asan_report_load8_noabort(uintptr_t addr)
    { AsanReport(addr, 8, false, __builtin_return_address(0)); }

    void __asan_report_load16_noabort(uintptr_t addr)
    { AsanReport(addr, 16, false, __builtin_return_address(0)); }
    
    void __asan_report_load_n_noabort(uintptr_t addr, size_t size)
    { AsanReport(addr, size, false, __builtin_return_address(0)); }

    void __asan_report_store1_noabort(uintptr_t addr)
    { AsanReport(addr, 1, true, __builtin_return_address(0)); }

    void __asan_report_store2_noabort(uintptr_t addr)
    { AsanReport(addr, 2, true, __builtin_return_address(0)); }

    void __asan_report_store4_noabort(uintptr_t addr)
    { AsanReport(addr, 4, true, __builtin_return_address(0)); }

    void __asan_report_store8_noabort(uintptr_t addr)
    { AsanReport(addr, 8, true, __builtin_return_address(0)); }

    void __asan_report_store16_noabort(uintptr_t addr)
    { AsanReport(addr, 16, true, __builtin_return_address(0)); }

    void __asan_report_store_n_noabort(uintptr_t addr, size_t size)
    { AsanReport(addr, size, true, __builtin_return_address(0)); }

    void __asan_load1_noabort(uintptr_t addr)
    { AsanAccess(addr, 1, false, __builtin_return_address(0)); }

    void __asan_load2_noabort(uintptr_t addr)
    { AsanAccess(addr, 2, false, __builtin_return_address(0)); }

    void __asan_load4_noabort(uintptr_t addr)
    { AsanAccess(addr, 4, false, __builtin_return_address(0)); }

    void __asan_load8_noabort(uintptr_t addr)
    { AsanAccess(addr, 8, false, __builtin_return_address(0)); }

    void __asan_load16_noabort(uintptr_t addr)
    { AsanAccess(addr, 16, false, __builtin_return_address(0)); }
    
    void __asan_loadN_noabort(uintptr_t addr, size_t size)
    { AsanAccess(addr, size, false, __builtin_return_address(0)); }

    void __asan_store1_noabort(uintptr_t addr)
    { AsanAccess(addr, 1, true, __builtin_return_address(0)); }

    void __asan_store2_noabort(uintptr_t addr)
    { AsanAccess(addr, 2, true, __builtin_return_address(0)); }

    void __asan_store4_noabort(uintptr_t addr)
    { AsanAccess(addr, 4, true, __builtin_return_address(0)); }

    void __asan_store8_noabort(uintptr_t addr)
    { AsanAccess(addr, 8, true, __builtin_return_address(0)); }

    void __asan_store16_noabort(uintptr_t addr)
    { AsanAccess(addr, 16, true, __builtin_return_address(0)); }

    void __asan_storeN_noabort(uintptr_t addr, size_t size)
    { AsanAccess(addr, size, true, __builtin_return_address(0)); }

    void __asan_load1(uintptr_t addr)
    { AsanAccess(addr, 1, false, __builtin_return_address(0)); }

    void __asan_load2(uintptr_t addr)
    { AsanAccess(addr, 2, false, __builtin_return_address(0)); }

    void __asan_load4(uintptr_t addr)
    { AsanAccess(addr, 4, false, __builtin_return_address(0)); }

    void __asan_load8(uintptr_t addr)
    { AsanAccess(addr, 8, false, __builtin_return_address(0)); }

    void __asan_load16(uintptr_t addr)
    { AsanAccess(addr, 16, false, __builtin_return_address(0)); }
    
    void __asan_load_n(uintptr_t addr, size_t size)
    { AsanAccess(addr, size, false, __builtin_return_address(0)); }

    void __asan_store1(uintptr_t addr)
    { AsanAccess(addr, 1, true, __builtin_return_address(0)); }

    void __asan_store2(uintptr_t addr)
    { AsanAccess(addr, 2, true, __builtin_return_address(0)); }

    void __asan_store4(uintptr_t addr)
    { AsanAccess(addr, 4, true, __builtin_return_address(0)); }

    void __asan_store8(uintptr_t addr)
    { AsanAccess(addr, 8, true, __builtin_return_address(0)); }

    void __asan_store16(uintptr_t addr)
    { AsanAccess(addr, 16, true, __builtin_return_address(0)); }

    void __asan_store_n(uintptr_t addr, size_t size)
    { AsanAccess(addr, size, true, __builtin_return_address(0)); }

    void __asan_after_dynamic_init()
    {} //no-op

    void __asan_before_dynamic_init()
    {} //no-op

    void __asan_handle_no_return()
    {} //no-op

    void __asan_register_globals()
    {} //no-op

    void __asan_unregister_globals()
    {} //no-op
}
