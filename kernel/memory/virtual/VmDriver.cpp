#include <memory/virtual/VmDriver.h>
#include <memory/virtual/AnonVmDriver.h>
#include <memory/virtual/KernelVmDriver.h>
#include <memory/virtual/VfsVmDriver.h>
#include <debug/Log.h>
#include <Lazy.h>

namespace Npk::Memory::Virtual
{
    sl::Lazy<AnonVmDriver> anonDriver;
    sl::Lazy<KernelVmDriver> kernelDriver;
    sl::Lazy<VfsVmDriver> vfsDriver;

    VmDriver* VmDriver::GetDriver(VmFlags flags)
    { 
        if (flags.Has(VmFlag::Anon)) //TODO: a more elegant solution, same goes with the Lazy instantiations above
            return &*anonDriver;
        if (flags.Has(VmFlag::File))
            return &*vfsDriver;
        if (flags.Has(VmFlag::Mmio))
            return &*kernelDriver;
        return nullptr;
    }

    void VmDriver::InitAll()
    {
        //the init functions take a bitmap of which driver-specific features to enable.
        //we enable everything for now!
        anonDriver.Init().Init(~0);
        kernelDriver.Init().Init(~0);
        vfsDriver.Init().Init(~0);
    }
}
