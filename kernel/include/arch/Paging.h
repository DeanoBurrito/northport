#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>

namespace Npk
{
#ifdef __x86_64__
    enum PageFlags : uint64_t
    {
        None = 0,
        Write = 1 << 1,
        User = 1 << 2,
        Global = 1 << 8,
        Execute = 1ul << 63,
    };

    enum class PageSizes : size_t
    {
        _4K = 1,
        _2M = 2,
        _1G = 3
    };

    constexpr inline size_t GetPageSize(PageSizes size)
    { return 1ul << (12 + 9 * ((size_t)size - 1)); }
#elif __riscv_xlen == 64
#endif

    constexpr PageFlags operator|(const PageFlags& a, const PageFlags& b)
    { return (PageFlags)((size_t)a | (size_t)b); }

    constexpr PageFlags operator|=(PageFlags& src, const PageFlags& other)
    { return src = (PageFlags)((uintptr_t)src | (uintptr_t)other); }

    extern void* kernelMasterTables;
    extern uint32_t kernelTablesGen;

    void PagingSetup();
    bool MapMemory(void* root, uintptr_t vaddr, uintptr_t paddr, PageFlags flags, PageSizes size, bool flushEntry);
    //NOTE: the physical address and page size are returned via ref args
    bool UnmapMemory(void* root, uintptr_t vaddr, uintptr_t& paddr, PageSizes& size, bool flushEntry);
    sl::Opt<uintptr_t> GetPhysicalAddr(void* root, uintptr_t virt);
    void SyncKernelTables(void* dest);
    void LoadTables(void* root);
    PageSizes MaxSupportedPagingSize();
}
