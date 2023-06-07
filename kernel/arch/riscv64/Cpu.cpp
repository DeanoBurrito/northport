#include <arch/Cpu.h>
#include <debug/Log.h>
#include <config/DeviceTree.h>

namespace Npk
{
    const char* isaString = nullptr;
    
    void ScanCpuFeatures()
    {
        //TODO: we assume homogenous cpu setup here (same with x86_64)
        auto cpuNode = Config::DeviceTree::Global().GetNode("/cpus/cpu@0");
        ASSERT(cpuNode.HasValue(), "");
        isaString = cpuNode->GetProp("riscv,isa")->ReadStr();
    }

    void LogCpuFeatures()
    {
        const char* model = "<not found>";
        auto modelProp = Config::DeviceTree::Global().GetNode("/")->GetProp("model");
        if (modelProp)
            model = modelProp->ReadStr();
        Log("Model: %s", LogLevel::Info, model);
        Log("ISA String: %s", LogLevel::Info, isaString == nullptr ? "<not found>" : isaString);
    }

    bool CpuHasFeature(CpuFeature feature)
    {
        if (isaString == nullptr)
            return false;
        
        switch (feature)
        {
        case CpuFeature::Sstc:
            return false;
        case CpuFeature::VGuest:
            return false;
        case CpuFeature::SingleFPU:
            return false;
        case CpuFeature::DoubleFPU:
            return false;
        case CpuFeature::QuadFPU:
            return false;

        default:
            return false;
        }
    }

    const char* CpuFeatureName(CpuFeature feature)
    {
        switch (feature)
        {
        case CpuFeature::Sstc:
            return "sstc";
        case CpuFeature::VGuest:
            return "vguest";
        case CpuFeature::SingleFPU:
            return "F";
        case CpuFeature::DoubleFPU:
            return "D";
        case CpuFeature::QuadFPU:
            return "Q";
        default:
            return "";
        }
    }
}
