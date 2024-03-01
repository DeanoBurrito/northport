#include <arch/Cpu.h>
#include <debug/Log.h>
#include <config/AcpiTables.h>
#include <config/DeviceTree.h>
#include <NanoPrintf.h>
#include <Bitmap.h>
#include <Span.h>
#include <Maths.h>
#include <Memory.h>
#include <NativePtr.h>

namespace Npk
{
    void ScanGlobalTopology()
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
        //Try get the isa string from the acpi table (RHCT) if it exists,
        //fallback to the device tree otherwise.

        sl::StringSpan isaString {};
        bool usedAcpi = true;
        if (auto maybeRhct = FindAcpiTable(SigRhct); maybeRhct.HasValue())
        {
            const Rhct* rhct = static_cast<const Rhct*>(*maybeRhct);
            const RhctNodes::HartInfoNode* found = nullptr;

            while (true)
            {
                auto maybeNode = FindRhctNode(rhct, RhctNodeType::HartInfo, found);
                if (!maybeNode)
                {
                    found = nullptr;
                    break; //we're done, exit and leave found empty
                }
                found = static_cast<const RhctNodes::HartInfoNode*>(*maybeNode);

                if (found->acpiProcessorId != CoreLocal().acpiId)
                    continue; //wrong hart
                break; //we found the node, exit and leave it in `prev`
            }

            ASSERT(found != nullptr, "Could not find hart's RHCT info node");
            sl::CNativePtr rhctAccess = rhct;
            for (size_t i = 0; i < found->offsetCount; i++)
            {
                const RhctNode* node = rhctAccess.Offset(found->offsets[i]).As<RhctNode>();
                if (node->type != RhctNodeType::IsaString)
                    continue;

                //TODO: this is hilariously broken, we need to be sensitive to underscores (look at searching for single letter extensions)
                auto isaNode = static_cast<const RhctNodes::IsaStringNode*>(node);
                isaString = sl::StringSpan(reinterpret_cast<const char*>(isaNode->str), isaNode->strLength);
                break;
            }
        }
        else if (DeviceTree::Global().Available())
        {
            const size_t nodeNameLength = npf_snprintf(nullptr, 0, "/cpus/cpu@%lu", CoreLocal().id) + 1;
            char nodeName[nodeNameLength];
            npf_snprintf(nodeName, nodeNameLength, "/cpus/cpu@%lu", CoreLocal().id);

            const DtNode* cpuNode = DeviceTree::Global().Find({ nodeName, nodeNameLength - 1 });
            ASSERT(cpuNode != nullptr, "No device tree node for cpu");
            DtProp* isaProp = cpuNode->FindProp("riscv,isa");
            ASSERT(isaProp != nullptr, "No isa string for cpu");

            isaString = isaProp->ReadString(0);
            usedAcpi = false;
        }
        else
            ASSERT(false, "Cannot get ISA string");

        Log("ISA string found via %s: %s", LogLevel::Verbose, usedAcpi ? "acpi" : "dtb",
            isaString.Begin());

        //found the isa string, no create a bitmap of features we care about
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
