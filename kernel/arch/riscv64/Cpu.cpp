#include <arch/Cpu.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <config/DeviceTree.h>
#include <Bitmap.h>
#include <Span.h>
#include <Maths.h>
#include <Memory.h>

namespace Npk
{
    void InitTopology()
    {
        Log("Topological mapping is not currently available on riscv.", LogLevel::Warning);
        //TODO: map using device tree
    }

    void ScanLocalTopology()
    {} //no-op

    NumaDomain* rootDomain;

    NumaDomain* GetTopologyRoot()
    { return rootDomain; }

    struct CpuFeatureAccessor
    {
        const char* str;
        bool custom = false;
    };

    constexpr static CpuFeatureAccessor accessors[] =
    {
        { .str = "vguest", .custom = true },
        { .str = "sstc" },
        { .str = "f" },
        { .str = "d" },
        { .str = "q" },
    };

    static_assert((size_t)CpuFeature::Count == (sizeof(accessors) / sizeof(CpuFeatureAccessor)), "Forgot to update FeatureNames array?");

    void ScanLocalCpuFeatures()
    {
        using namespace Config;
        const size_t nodeNameLength = npf_snprintf(nullptr, 0, "/cpus/cpu@%lu", CoreLocal().id) + 1;
        char nodeName[nodeNameLength];
        npf_snprintf(nodeName, nodeNameLength, "/cpus/cpu@%lu", CoreLocal().id);

        const DtNode* cpuNode = DeviceTree::Global().Find({ nodeName, nodeNameLength - 1 });
        ASSERT(cpuNode != nullptr, "No device tree node for cpu");
        DtProp* isaProp = cpuNode->FindProp("riscv,isa");
        ASSERT(isaProp != nullptr, "No isa string for cpu");
        sl::StringSpan isaString = isaProp->ReadString(0);
        Log("Isa string: %s", LogLevel::Verbose, isaString.Begin());

        const size_t bitmapBytes = sl::AlignUp((size_t)CpuFeature::Count, 8) / 8;
        uint8_t* bitmap = new uint8_t[bitmapBytes];
        sl::memset(bitmap, 0, bitmapBytes);

        for (size_t i = 0; i < (size_t)CpuFeature::Count; i++)
        {
            if (accessors[i].custom)
                continue;

            sl::StringSpan accessorSpan { accessors[i].str, sl::memfirst(accessors[i].str, 0, 0) };
            if (isaString.Contains(accessorSpan))
                sl::BitmapSet(bitmap, i);
        };

        CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        config->featureBitmap = bitmap;
    }

    bool CpuHasFeature(CpuFeature feature)
    {
        ASSERT(CoreLocal()[LocalPtr::Config] != nullptr, "Too early to check for cpu features");
        const CoreConfig* config = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        ASSERT(config->featureBitmap != nullptr, "Too early to check for cpu features");

        return sl::BitmapGet(config->featureBitmap, (size_t)feature);
    }

    const char* CpuFeatureName(CpuFeature feature)
    {
        const size_t featIndex = static_cast<size_t>(feature);
        if (featIndex >= (size_t)CpuFeature::Count)
            return "<unknown feature>";
        return accessors[featIndex].str;
    }
}
