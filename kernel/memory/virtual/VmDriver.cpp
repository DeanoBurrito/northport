#include <memory/virtual/VmDriver.h>
#include <memory/virtual/AnonVmDriver.h>
#include <memory/virtual/KernelVmDriver.h>
#include <debug/Log.h>

namespace Npk::Memory::Virtual
{
    alignas(AnonVmDriver) uint8_t anonDriver[sizeof(AnonVmDriver)];
    alignas(KernelVmDriver) uint8_t kernelDriver[sizeof(KernelVmDriver)];
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
        vmDrivers[(size_t)VmDriverType::Anon] = new (anonDriver) AnonVmDriver();
        vmDrivers[(size_t)VmDriverType::Kernel] = new (kernelDriver) KernelVmDriver();

        for (size_t i = 1; i < (size_t)VmDriverType::EnumCount; i++)
        {
            if (vmDrivers[i] == nullptr)
                continue;
            vmDrivers[i]->Init();
            Log("VM driver initialized: index=%lu, name=%s", LogLevel::Verbose, i, VmDriverTypeStrs[i]);
        }
    }
}
