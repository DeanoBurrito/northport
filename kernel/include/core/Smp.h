#pragma once

#include <core/Event.h>
#include <core/Defs.h>
#include <Locks.h>

namespace Npk::Core
{
    struct SmpInfo
    {
        size_t cpuCount;
        uintptr_t localsBase;
        uintptr_t idleStacksBase;
        size_t localsStride;

        struct
        {
            size_t mailbox;
        } offsets;
    };

    extern SmpInfo smpInfo;

    SL_ALWAYS_INLINE
    void* GetPerCpu(CpuId cpu, size_t offset = 0)
    {
        const uintptr_t base = smpInfo.localsBase + (cpu * smpInfo.localsStride);
        return reinterpret_cast<void*>(base + offset);
    }

    SL_ALWAYS_INLINE
    void* GetMyPerCpu(size_t offset)
    {
        return GetPerCpu(CoreId(), offset);
    }

    using MailCallback = void (*)(void* arg);

    struct MailboxEntry
    {
        sl::FwdListHook listHook;

        MailCallback callback;
        void* arg;
        Waitable* onComplete;
    };

    struct MailboxControl
    {
        sl::Atomic<bool> remotePanic;
        sl::SpinLock lock;
        sl::Span<MailboxEntry> entries;
        sl::FwdList<MailboxEntry, &MailboxEntry::listHook> pending;
        sl::FwdList<MailboxEntry, &MailboxEntry::listHook> free;
    };

    bool MailToOne(CpuId who, MailboxEntry mail, bool urgent);
    bool MailToMany(sl::Span<CpuId> who, MailboxEntry mail, bool urgent);
    bool MailToSet(CpuBitset who, MailboxEntry mail, bool urgent);
    bool MailToAll(MailboxEntry mail, bool urgent, bool includeSelf);
    //TODO: TLB sync functions

    void PanicAllCores();
}
