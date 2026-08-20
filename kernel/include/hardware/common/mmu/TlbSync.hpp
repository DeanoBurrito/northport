#pragma once

#include <Core.hpp>

/* This header + the corresponding source file implement a software tlb sync
 * mechanism for cases where there is no hardware support for this. This code
 * is portable and doesn't need to know about what the tlb actually caches,
 * so it can be used with MMUs that dont use page tables. It just deals in cpus,
 * virtual address ranges and ASIDs.
 * There is also a deferred free mechanism that ties into the tlb sync, allowing
 * lists of pages to be freed only once all references to them have be flushed.
 * This is useful for implementing the `freeAfter` page list that
 * `HwUpdateMap()` accepts.
 */
namespace Npk
{
    enum class TlbSyncStat
    {
        Deposits,
        SlotDeposits,
        DepositOverflows,
        DeferredPages,
        FreedPages,
        DeferredFreeOverflows,
        Drains,
        DrainedSlots,
        DrainedPages,
        IpisSent,

        Count
    };

    using TlbSyncStats = sl::Stats<TlbSyncStat, (size_t)TlbSyncStat::Count>;

    NpkStatus InitSoftwareTlbSync();

    NpkStatus GetTlbSyncStats(TlbSyncStats& stats);

    /* Coordinates TLB eviction for `vaddr` -> `vaddr + length` for any entries
     * tagged with `asid`. The `targets` bitmap is used to reduce the number
     * of cpus (set bits indicate the cpu should be involved), or it can be
     * left null if all cpus should be involved. Note that `targets` is indexed
     * using relative cpu ids (relative to the SystemDomain base id).
     * This function also handles flushing on the local cpu if needed.
     *
     * This function does **not** wait for remote cpus to action and confirm
     * the flush requests, that can be done with `TlbSyncWait()`.
     */
    void TlbSyncDeposit(const CpuBitset* targets, Asid asid, uintptr_t vaddr,
        size_t length);

    /* Similar to `Deposit()` but flushes all cached entries associated with an
     * ASID.
     */
    void TlbSyncDepositAll(const CpuBitset* targets, Asid asid);

    /* RCU-style freeing for a list of pages: once all cpus are beyond the
     * current shootdown epoch the pages in the list are all freed. This is
     * useful for unmapping and then freeing pages: first Deposit() can be
     * called on the virtual address ranges + ASIDs and then the backing pages
     * can be formed into a list and passed to this function.
     */
    void TlbSyncReclaim(PageList& pages);

    /* Must be called at passive IPL. Blocks the current thread until all
     * deposits made by this cpu before calling this function have completed,
     * meaning all no TLBs share stale views of ranges deposited before this
     * call. This is mainly used by code that needs to wait for a shootdown
     * to be acknowledged and actioned by all TLBs before proceeding, like an
     * unmap or downgrade of permissions.
     */
    void TlbSyncWait();

    /* Calling this function indicates the current cpu is passing a quiescent
     * point with regards to the local TLB. Any pending tlb ops are processed
     * here.
     * This function must be called when lowering IPL from Tlb level.
     */
    void TlbSyncQuiesce();

    /* TODO: docs + activate when cpu is idle or offline.
     */
    void BeginNoTlbSyncEpoch();

    /*
     */
    void EndNoTlbSyncEpoch();
}
