#include <arch/Cpu.h>

namespace Npk
{
    void ScanGlobalTopology()
    {} //no-op

    void ScanLocalTopology()
    {} //no-op

    NumaDomain* GetTopologyRoot()
    { return nullptr; }

    void ScanLocalCpuFeatures()
    {} //no-op

    bool CpuHasFeature(CpuFeature feature)
    { return false; }

    const char* CpuFeatureName(CpuFeature feature)
    { return nullptr; }
}
