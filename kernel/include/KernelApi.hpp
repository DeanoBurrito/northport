#pragma once

#include <KernelTypes.hpp>
#include <containers/LruCache.h>

namespace Npk
{
    namespace Internal
    {
        bool PmaCacheSetEntry(size_t slot, void** curVaddr, Paddr curPaddr, Paddr nextPaddr);
    };

    extern SystemDomain domain0;

    SL_PRINTF_FUNC(1, 3)
    void Log(const char* msg, LogLevel level, ...);
    [[noreturn]]
    void Panic(sl::StringSpan message);

    void AddLogSink(LogSink& sink);
    void RemoveLogSink(LogSink& sink);
    sl::StringSpan LogLevelStr(LogLevel level);

    void AssertIpl(Ipl target);
    Ipl CurrentIpl();
    Ipl RaiseIpl(Ipl target);
    void LowerIpl(Ipl target);
    sl::StringSpan IplStr(Ipl which);

    template<Ipl min, Ipl max>
    inline void IplSpinLock<min, max>::Lock()
    {
        prevIpl = CurrentIpl();
        if (prevIpl > max || min > prevIpl)
            Panic("Bad IPL when acquiring IplSpinLock");

        RaiseIpl(max);
        lock.Lock();
    }

    template<Ipl min, Ipl max>
    inline bool IplSpinLock<min, max>::TryLock()
    {
        auto lastIpl = CurrentIpl();
        if (lastIpl > max || min > lastIpl)
            Panic("Bad IPL when trying to acquire IplSpinLock");

        RaiseIpl(max);
        const bool success = lock.TryLock();

        if (success)
            prevIpl = lastIpl;
        else
            LowerIpl(prevIpl);

        return success;
    }

    template<Ipl min, Ipl max>
    inline void IplSpinLock<min, max>::Unlock()
    {
        if (prevIpl < max)
            LowerIpl(prevIpl);
        lock.Unlock();
    }

    void QueueDpc(Dpc* dpc);
    RemoteCpuStatus* RemoteStatus(CpuId who);
    void SendMail(CpuId who, SmpMail* mail);
    void FlushRemoteTlbs(sl::Span<CpuId> who, RemoteFlushRequest* what, bool sync);
    void SetMyIpiId(void* id);
    void* GetIpiId(CpuId id);
    void NudgeCpu(CpuId who);
    
    CycleAccount SetCycleAccount(CycleAccount who);
    void AddClockEvent(ClockEvent* event);
    bool RemoveClockEvent(ClockEvent* event);
    sl::TimePoint GetTime();
    sl::TimePoint GetTimeOffset();
    void SetTimeOffset(sl::TimePoint offset);

    SL_ALWAYS_INLINE
    sl::TimePoint GetMonotonicTime()
    {
        return PlatReadTimestamp();
    }

    void SetConfigStore(sl::StringSpan store, bool noLog);
    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue);
    sl::StringSpan ReadConfigString(sl::StringSpan key, sl::StringSpan defaultValue);

    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type);
    sl::Opt<Sdt*> GetAcpiTable(sl::StringSpan signature);

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

    SystemDomain& MySystemDomain();

    SL_ALWAYS_INLINE
    KernelMap* MyKernelMap()
    {
        return &MySystemDomain().kernelSpace;
    }

    PageInfo* AllocPage(bool canFail);
    void FreePage(PageInfo* page);

    using PageAccessCache = sl::LruCache<Paddr, void*, Internal::PmaCacheSetEntry>;
    using PageAccessRef = PageAccessCache::CacheRef;

    size_t CopyFromPhysical(Paddr base, sl::Span<char> buffer);
    void InitPageAccessCache(size_t entries, uintptr_t slots);
    PageAccessRef AccessPage(Paddr paddr);

    SL_ALWAYS_INLINE
    PageAccessRef AccessPage(PageInfo* page)
    {
        return AccessPage(LookupPagePaddr(page));
    }

    void CancelWait(ThreadContext* thread);
    WaitStatus WaitMany(sl::Span<Waitable*> what, WaitEntry* entries, sl::TimeCount timeout, sl::StringSpan reason = {});
    void SignalWaitable(Waitable* what);
    void ResetWaitable(Waitable* what, WaitableType newType);

    SL_ALWAYS_INLINE
    WaitStatus WaitOne(Waitable* what, WaitEntry* entry, sl::TimeCount timeout, sl::StringSpan reason = {})
    {
        return WaitMany({ &what, 1 }, entry, timeout, reason);
    }
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
