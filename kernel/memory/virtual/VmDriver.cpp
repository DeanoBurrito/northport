#include <memory/virtual/VmDriver.h>
#include <memory/virtual/AnonVmDriver.h>
#include <memory/virtual/KernelVmDriver.h>
#include <debug/Log.h>
#include <Lazy.h>

namespace Npk::Memory::Virtual
{
    sl::Lazy<AnonVmDriver> anonDriver;
    sl::Lazy<KernelVmDriver> kernelDriver;
    VmDriver* vmDrivers[(size_t)VmDriverType::EnumCount];

    VmDriver* VmDriver::GetDriver(VmDriverType name)
    { 
        const size_t nameInt = (size_t)name;
        if (nameInt == 0 || nameInt >= (size_t)VmDriverType::EnumCount)
            return nullptr;
        return vmDrivers[(size_t)name];
    }

    void VmDriver::InitEarly()
    {
        vmDrivers[(size_t)VmDriverType::Anon] = &anonDriver.Init();
        vmDrivers[(size_t)VmDriverType::Kernel] = &kernelDriver.Init();

        for (size_t i = 1; i < (size_t)VmDriverType::EnumCount; i++)
        {
            if (vmDrivers[i] == nullptr)
                continue;
            vmDrivers[i]->Init();
            Log("VM driver initialized: index=%lu, name=%s", LogLevel::Verbose, i, VmDriverTypeStrs[i]);
        }
    }
}
