#pragma once

#include <KernelTypes.hpp>
#include <containers/LruCache.h>

namespace Npk
{
    namespace Internal
    {
        bool PmaCacheSetEntry(size_t slot, void** curVaddr, Paddr curPaddr, Paddr nextPaddr);
    };

    extern MemoryDomain domain0;

    SL_PRINTF_FUNC(1, 3)
    void Log(const char* msg, LogLevel level, ...);
    [[noreturn]]
    void Panic(sl::StringSpan message);

    void AddLogSink(LogSink& sink);
    void RemoveLogSink(LogSink& sink);
    sl::StringSpan LogLevelStr(LogLevel level);

    void SetConfigStore(sl::StringSpan store);
    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue);
    sl::StringSpan ReadConfigString(sl::StringSpan key, sl::StringSpan defaultValue);

    SL_ALWAYS_INLINE
    PageInfo* LookupPageInfo(Paddr paddr)
    {
        return &domain0.pfndb[((paddr - domain0.physOffset) >> PfnShift())];
    }

    SL_ALWAYS_INLINE
    Paddr LookupPagePaddr(PageInfo* info)
    {
        return ((info - domain0.pfndb) << PfnShift()) + domain0.physOffset;
    }

    PageInfo* AllocPage();
    void FreePage(PageInfo* page);

    using PageAccessCache = sl::LruCache<Paddr, void*, Internal::PmaCacheSetEntry>;
    using PageAccessRef = PageAccessCache::CacheRef;

    size_t CopyFromPages(Paddr base, sl::Span<char> buffer);
    void InitPageAccessCache(size_t entries, PageAccessCache::Slot* slots, Paddr defaultPaddr);
    PageAccessRef AccessPage(Paddr paddr);

    SL_ALWAYS_INLINE
    PageAccessRef AccessPage(PageInfo* page)
    {
        return AccessPage(LookupPagePaddr(page));
    }

    WaitResult WaitOne(Waitable* what, WaitEntry* entry, sl::TimeCount timeout, WaitFlags flags);
    WaitResult WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, sl::TimeCount timeout, WaitFlags flags);
}

#define NPK_ASSERT_STRINGIFY(x) NPK_ASSERT_STRINGIFY2(x)
#define NPK_ASSERT_STRINGIFY2(x) #x

#define NPK_ASSERT(cond) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Panic("Assert failed (" SL_FILENAME_MACRO ":" NPK_ASSERT_STRINGIFY(__LINE__) "): " #cond); \
    }

#define NPK_UNREACHABLE() \
    NPK_ASSERT(!"Unreachable code reached."); \
    SL_UNREACHABLE()

//TODO: print a backtrace of where the check failed?
#define NPK_CHECK(cond, ret) \
    if (SL_UNLIKELY(!(cond))) \
    { \
        Npk::Log("Check failed %s:%i: %s, caller=%p", LogLevel::Error, \
            SL_FILENAME_MACRO, __LINE__, #cond, SL_RETURN_ADDR); \
        return ret; \
    }
