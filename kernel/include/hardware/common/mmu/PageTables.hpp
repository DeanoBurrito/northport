#pragma once

#include <Hardware.hpp>

namespace Npk
{
    struct InitState;

    struct PageTableConfig
    {
        size_t levelCount;

        const uint8_t* levelShift;
        const uintptr_t* levelMask;
        const size_t* levelPtSize;

        uintptr_t leafLevelMask;
        size_t pteSize;

        size_t kernelFirstIndex;
        size_t kernelLastIndex;
        bool splitRoot;
        bool hwAccessedBit;
        bool hwDirtyBit;
        bool hasCustomWritePte;
        bool hasCustomExchange;
        bool hasCustomCompareExchange;
    };

    /* Returns the configuration of the page tables the MMU expects on this
     * system.
     */
    const PageTableConfig& GetPageTableConfig();

    /* Sets the paddr of the root table the local MMMU should use for user
     * address translations, and the local ASID associated with it.
     */
    void SetUserRoot(Paddr root, Asid asid);

    /* Returns the current paddr for the root user page table.
     */
    Paddr GetUserRoot();

    /* Sets up `*pte` to resolve translations through it to `paddr`, with the
     * specified permissions and cache mode. The `kernel` argument determines if
     * the translation is valid for user or kernel accesses, and `level` is
     * the page table level `*pte` exists in.
     */
    void MakeLeafPte(void* pte, Paddr paddr, MmuPermissions perms,
        MmuCacheMode cacheMode, bool kernel, size_t level);

    /* Sets up `*pte` such that it points to a lower level page table at
     * `child`. The `kernel` argument indicates if this PTE will be used for
     * user or kernel translations.
     */
    void MakeIntermediatePte(void* pte, Paddr child, bool kernel);

    /* Ensures `*pte` would be considered invalid by the MMU, this should not
     * be called on live PTEs: instead construct a PTE and then exchange it a
     * live PTE since this function isn't required to be atomic.
     */
    void MakeInvalidPte(void* pte);

    /*
     */
    void SetPtePerms(void* pte, MmuPermissions perms);

    /* Returns whether a PTE would be considered valid by the MMU.
     */
    bool IsPteValid(const void* pte);

    /* Returns whether a PTE is a leaf (translation ends here).
     */
    bool IsLeafPte(const void* pte, size_t level);

    /* Extracts and returns the physical address encoded in a PTE.
     */
    Paddr GetPteAddr(const void* pte);

    /* Extracts and returns the current flags encoded in the PTE. Note that
     * this may not be the requested flags if hardware is unable to enforce a
     * a requested feature. E.g. some architectures dont have a control bit for
     * instruction fetches, or a caching mode appropriate to all intents.
     */
    MmuPermissions GetPtePerms(const void* pte);

    /* Similar to GetPteFlags(), but attempts to reconstruct the cache mode
     * of a PTE.
     */
    MmuCacheMode GetPteCacheMode(const void* pte);

    /* Returns whether a PTE has the accessed bit set.
     */
    bool IsPteAccessed(const void* pte);

    /* Returns whether a PTE has the dirty bit set.
     */
    bool IsPteDirty(const void* pte);

    /* Atomically publish the pte at `source` to `dest`. The pte at `dest`
     * must be observed invalid by all cpus prior to this call.
     *
     * This function is only called if `hasCustomWritePte` is set, if clear
     * a portable default implementation will be used.
     */
    void WritePte(void* dest, const void* source);

    /* Performs an atomic exchange: `dest` is placed into `prev`, and
     * `source` is placed into `dest`.
     *
     * This function is only called if `hasCustomExchange` is set, if clear a
     * portable default implementation will be used.
     */
    void ExchangePte(void* dest, const void* source, void* prev);

    /* Performs a compare-exchange as per the C++ standard operation.
     * The `dest`, `expected` and `desired` arguments correspond to the
     * standard arguments defined by the spec.
     * To save you opening the extra window this operation places
     * `desired` into `dest` if `dest == expected`. If the equality check
     * fails, the current value of `dest` is placed into `expected`.
     * Returns whether `dest` was updated or not.
     *
     * This function is only set if `hasCustomCompareExchange` is set, if clear
     * a portable default implementation will be used.
     */
    bool CompareExchangePte(void* dest, void* expected, const void* desired);

    /* Atomically clears the accessed bit of a live PTE, returns the previous
     * state of the bit.
     */
    bool ClearPteAccessed(void* pte);

    /* Atommically clears the dirty bit of a live PTE, returns the previous
     * state of the bit.
     */
    bool ClearPteDirty(void* pte);

    /* Atomically set the accessed (A) bit of a leaf PTE. Note that the
     * PTE is live and accessible by the MMU and likely already cached by
     * it.
     * Returns the prior state of the A bit.
     *
     * This function is only called if `hwAccessedBit` is clear.
     */
    bool SetPteAccessed(void* pte);

    /* Similar to `SetAccessed()` above but operates on the dirty (D) bit
     * of a live leaf PTE.
     *
     * This function is only called if `hwAccessedBit` and `hwDirtyBit` are 
     * clear.
     */
    bool SetPteDirty(void* pte);

    /* Determines if a leaf PTE is logically writable but the dirty bit
     * hasn't been set yet. If this function returns true a write fault on
     * this leaf is a minor page fault (wont invoke VM machinery) and 
     * instead will call `SetDirty()` (see above) on the PTE.
     * 
     * This function is only called if `hwAccessedBit` and `hwDirtyBit` are 
     * clear.
     */
    bool PteIsWriteTrackable(const void* pte);

    /* Upcall from arch specific code, builds the first kernel map using early
     * allocators and early map management functions. 
     */
    HwMap* HwCreateKernelMap(InitState& state, Paddr root);
}
