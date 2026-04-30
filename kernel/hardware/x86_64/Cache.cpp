#include <Hardware.hpp>
#include <Core.hpp>
#include <hardware/x86_64/Cpuid.hpp>

#define SERIALIZING_INSTRUCTION() \
do \
{ \
    asm volatile("cpuid" ::: "eax", "ebx", "ecx", "edx", "memory"); \
} while(false)

namespace Npk
{
    enum class FlushInstr
    {
        ClFlush,
        ClFlushOpt,
        ClWb,
    };

    static void FlushDataCache(uintptr_t base, size_t len, size_t cacheLineSize, 
        FlushInstr instr)
    {
        const uintptr_t top = base + len;
        base = base & ~(cacheLineSize - 1);

        while (base < top)
        {
            switch (instr)
            {
            case FlushInstr::ClFlush:
                asm volatile("clflush (%0)" :: "r"(base) : "memory");
                break;

            case FlushInstr::ClFlushOpt:
                asm volatile("clflushopt (%0)" :: "r"(base) : "memory");
                break;

            case FlushInstr::ClWb:
                asm volatile("clwb (%0)" :: "r"(base) : "memory");
                break;
            }

            base += cacheLineSize;
        }

        asm volatile("sfence" ::: "memory");
    }

    void HwFlushCache(uintptr_t base, size_t length, HwCacheOps ops, 
        HwCacheTypes types)
    {
        if (!ops.Any())
            return;
        if (!types.Any())
            return;
        if (length == 0)
            return;

        if (!CpuHasFeature(CpuFeature::ClFlush))
            Panic("CPU does not support CLFLUSH instruction", nullptr);

        const bool hasFlushOpt = CpuHasFeature(CpuFeature::ClFlushOpt);
        const bool hasClWb = CpuHasFeature(CpuFeature::ClWb);
        const bool doClean = ops.Has(HwCacheOp::Clean);
        const bool doInval = ops.Has(HwCacheOp::Invalidate);

        FlushInstr dcacheInstr =
            hasFlushOpt ? FlushInstr::ClFlushOpt : FlushInstr::ClFlush;
        if (doClean && !doInval && hasClWb)
            dcacheInstr = FlushInstr::ClWb;

        const FlushInstr icacheInstr = 
            hasFlushOpt ? FlushInstr::ClFlushOpt : FlushInstr::ClFlush;

        if (types.Has(HwCacheType::DCache))
        {
            if (doClean || doInval)
                FlushDataCache(base, length, HwGetCacheLineSize(), dcacheInstr);
            else if (ops.Has(HwCacheOp::CleanForCpus))
                asm volatile("sfence" ::: "memory");
        }

        if (types.Has(HwCacheType::ICache))
        {
            if ((doClean || doInval) && !types.Has(HwCacheType::DCache))
                FlushDataCache(base, length, HwGetCacheLineSize(), icacheInstr);

            SERIALIZING_INSTRUCTION();
        }
    }

    void HwFlushCacheAll(HwCacheOps ops, HwCacheTypes types)
    {
        if (!ops.Any())
            return;
        if (!types.Any())
            return;

        if (ops.Has(HwCacheOp::Clean) || ops.Has(HwCacheOp::Invalidate))
            asm volatile("wbinvd" ::: "memory");
        else if (ops.Has(HwCacheOp::CleanForCpus))
        {
            asm volatile("mfence" ::: "memory");

            if (types.Has(HwCacheType::ICache))
                SERIALIZING_INSTRUCTION();
        }
    }

    size_t HwGetCacheLineSize()
    {
        return 64; //sometimes I love this architecture.
    }
}
