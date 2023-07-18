#include <arch/Cpu.h>
#include <debug/Log.h>
#include <Memory.h>
#include <Maths.h>
#include <Bitmap.h>

namespace Npk
{
    struct CpuidLeaf
    {
        uint32_t a;
        uint32_t b;
        uint32_t c;
        uint32_t d;

        uint32_t operator[](uint8_t index)
        {
            switch (index)
            {
                case 'a': return a;
                case 'b': return b;
                case 'c': return c;
                case 'd': return d;
            }
            ASSERT_UNREACHABLE();
        }
    };

    CpuidLeaf& DoCpuid(uint32_t leaf, uint32_t subleaf, CpuidLeaf& data)
    {
        asm volatile("cpuid" : 
            "=a"(data.a), "=b"(data.b), "=c"(data.c), "=d"(data.d) :
            "0"(leaf), "2"(subleaf) :
            "memory");

        return data;
    }

    size_t GetCpuidCount(uint32_t ns)
    {
        CpuidLeaf leaf;
        return DoCpuid(ns, 0, leaf).a;
    }

    NumaDomain* rootDomain = nullptr;

    void InitTopology()
    {
        rootDomain = new NumaDomain();
        rootDomain->id = 0;
        rootDomain->cpus = nullptr;
        rootDomain->next = nullptr;
        rootDomain->memory = nullptr;
    }

    NumaDomain* GetTopologyRoot()
    { return rootDomain; }

    void ScanLocalTopology()
    {
        //asserting this ensures that we've parsed the SRAT table at some point, or failed to (in which case a dummy NUMA domain is generated).
        ASSERT(rootDomain != nullptr, "Cpu topology mapping failed, no known NUMA domains.");

        const size_t baseLeafCount = GetCpuidCount(0);
        size_t coreId; //which physical core we're part of
        size_t threadId; //which logical core we are within a physical core
        
        //the intel SDM recommends leaf 0x1F over 0xB, but we dont need
        //the extra reported data (we only care about cores vs threads)
        if (baseLeafCount >= 0xB)
        {
            CpuidLeaf leaf;
            const uint32_t threadShift = DoCpuid(0xB, 0, leaf).a & 0xF;
            const uint32_t coreShift = DoCpuid(0xB, 1, leaf).a & 0xF;
            
            uint32_t apicId = leaf.d;
            threadId = apicId & ((1 << threadShift) - 1);
            apicId >>= threadShift;
            coreId = apicId & ((1 << coreShift) - 1);
        }
        else if (baseLeafCount >= 0x4)
        { 
            ASSERT_UNREACHABLE(); 
        } //TODO: fallback
        else
        {
            Log("Unable to detect local processor topology", LogLevel::Warning);
            return;
        }

        ThreadDomain* thread = new ThreadDomain();
        thread->id = threadId;
        thread->next = nullptr;

        //check if cpu domain has already been allocated
        NumaDomain* numaDom = rootDomain;
        CpuDomain* cpuDom = nullptr;
        while (numaDom != nullptr)
        {
            numaDom->cpusLock.WriterLock();
            CpuDomain* scan = numaDom->cpus;
            while (scan != nullptr)
            {
                if (scan->id != coreId)
                {
                    scan = scan->next;
                    continue;
                }
                cpuDom = scan;
                break;
            }

            if (cpuDom != nullptr)
                break;

            numaDom->cpusLock.WriterUnlock();
            numaDom = numaDom->next;
        }

        if (cpuDom == nullptr)
        {
            if (numaDom == nullptr)
            {
                numaDom = rootDomain; //TODO: some way to get an "IDFK" numa domain
                numaDom->cpusLock.WriterLock();
            }

            cpuDom = new CpuDomain();
            cpuDom->id = coreId;
            cpuDom->parent = numaDom;
            cpuDom->next = nullptr;
            cpuDom->threads = nullptr;
            cpuDom->online = true;
            
            cpuDom->next = numaDom->cpus;
            numaDom->cpus = cpuDom;
        }

        //at this point we have a cpu domain + it's write lock
        thread->parent = cpuDom;
        thread->next = cpuDom->threads;
        cpuDom->threads = thread;
        numaDom->cpusLock.WriterUnlock();

        Log("Core %lu has topographic name %lu:%lu.%lu", LogLevel::Verbose, CoreLocal().id,
            numaDom->id, cpuDom->id, thread->id);
    }

    struct CpuFeatureAccessor
    {
        struct 
        { 
            uint32_t main;
            uint32_t sub;
        } leaf;
        uint8_t index;
        uint8_t shift;
        const char* name;
    };

    constexpr static CpuFeatureAccessor accessors[] =
    {
        { .leaf {1, 0}, .index = 'c', .shift = 31, .name = "vguest" },
        { .leaf {1, 0}, .index = 'd', .shift = 20, .name = "nx" },
        { .leaf {1, 0}, .index = 'd', .shift = 26, .name = "1g-map" },
        { .leaf {1, 0}, .index = 'd', .shift = 13, .name = "global-pages" },
        { .leaf {7, 0}, .index = 'b', .shift = 20, .name = "smap" },
        { .leaf {7, 0}, .index = 'b', .shift = 7, .name = "smep" },
        { .leaf {7, 0}, .index = 'c', .shift = 2, .name = "umip" },
        { .leaf {1, 0}, .index = 'd', .shift = 9, .name = "apic" },
        { .leaf {1, 0}, .index = 'c', .shift = 21, .name = "x2apic" },
        { .leaf {1, 0}, .index = 'd', .shift = 24, .name = "fxsave" },
        { .leaf {1, 0}, .index = 'c', .shift = 26, .name = "xsave" },
        { .leaf {1, 0}, .index = 'd', .shift = 0, .name = "fpu" },
        { .leaf {1, 0}, .index = 'd', .shift = 25, .name = "sse" },
        { .leaf {1, 0}, .index = 'd', .shift = 26, .name = "sse2" },
        { .leaf {6, 0}, .index = 'a', .shift = 2, .name = "arat" },
        { .leaf {1, 0}, .index = 'd', .shift = 4, .name = "tsc" },
        { .leaf {1, 0}, .index = 'c', .shift = 24, .name = "tsc-d" },
        { .leaf {7, 0}, .index = 'd', .shift = 8, .name = "inv-tsc" },
    };

    void ScanLocalCpuFeatures()
    {
        ASSERT(CoreLocalAvailable(), "Core-local store must be available");

        CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        const size_t bitmapLen = sl::AlignUp((size_t)CpuFeature::Count, 8) / 8;
        if (config->featureBitmap == nullptr)
            config->featureBitmap = new uint8_t[bitmapLen];

        sl::memset(config->featureBitmap, 0, bitmapLen);
        for (size_t i = 0; i < (size_t)CpuFeature::Count; i++)
        {
            CpuidLeaf leaf {};
            DoCpuid(accessors[i].leaf.main, accessors[i].leaf.sub, leaf);
            const uint32_t data = leaf[accessors[i].index];
            const bool present = data & (1ul << accessors[i].shift);

            if (present)
                sl::BitmapSet(config->featureBitmap, i);
        }
    }

    bool CpuHasFeature(CpuFeature feature)
    {
        const size_t featIndex = static_cast<size_t>(feature);
        if (featIndex >= (size_t)CpuFeature::Count)
            return false;

        if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Config] != nullptr)
        {
            auto config = static_cast<const CoreConfig*>(CoreLocal()[LocalPtr::Config]);
            if (config != nullptr && config->featureBitmap != nullptr)
                return sl::BitmapGet(config->featureBitmap, featIndex);
        }

        //fallback path: try to return correct info even if cache isnt available.
        //`cpuid` is a serializing instruction though, so this can slow things down.
        CpuidLeaf leaf {};
        DoCpuid(accessors[featIndex].leaf.main, accessors[featIndex].leaf.sub, leaf);
        const uint32_t data = leaf[accessors[featIndex].index];
        return data & (1ul << accessors[featIndex].shift);
    }

    static_assert((size_t)CpuFeature::Count == (sizeof(accessors) / sizeof(CpuFeatureAccessor)),
        "Forgot to update CpuFeatureAccessors array?");

    const char* CpuFeatureName(CpuFeature feature)
    {
        const size_t featIndex = static_cast<size_t>(feature);
        if (featIndex >= (size_t)CpuFeature::Count)
            return "<unknown feature>";

        return accessors[featIndex].name;
    }
}
