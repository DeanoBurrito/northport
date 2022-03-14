#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <devices/DeviceManager.h>
#include <Log.h>

namespace Kernel::Syscalls
{
    void FetchBasicFramebufferInfo(SyscallRegisters& regs)
    {
        auto maybeFramebuffer = Devices::DeviceManager::Global()->GetPrimaryDevice(Devices::DeviceType::GraphicsFramebuffer);
        if (!maybeFramebuffer)
        {
            regs.id = (uint64_t)np::Syscall::GetDeviceError::NoPrimaryDevice;
            return;
        }

        const auto* fb = static_cast<Devices::Interfaces::GenericFramebuffer*>(*maybeFramebuffer);
        const auto modeset = fb->GetCurrentMode();
        regs.arg0 = fb->GetId();

        regs.arg1  = modeset.width & 0xFFFF;
        regs.arg1 |= (modeset.height & 0xFFFF) << 16;
        regs.arg1 |= (modeset.width * modeset.bitsPerPixel / 8) << 32;
        regs.arg1 |= (modeset.bitsPerPixel & 0xFFFF) << 48;

        regs.arg2 = fb->GetAddress()->raw;

#define FORMAT_AT_OFFSET(field, bitOffset) (uint64_t)modeset.pixelFormat.field << bitOffset
        regs.arg3  = FORMAT_AT_OFFSET(redOffset, 0);
        regs.arg3 |= FORMAT_AT_OFFSET(greenOffset, 8);
        regs.arg3 |= FORMAT_AT_OFFSET(blueOffset, 16);
        regs.arg3 |= FORMAT_AT_OFFSET(alphaOffset, 24);
        regs.arg3 |= FORMAT_AT_OFFSET(redMask, 32);
        regs.arg3 |= FORMAT_AT_OFFSET(greenMask, 40);
        regs.arg3 |= FORMAT_AT_OFFSET(blueMask, 48);
        regs.arg3 |= FORMAT_AT_OFFSET(alphaMask, 56);
#undef FORMAT_AT_OFFSET
    }
    
    void GetPrimaryDeviceInfo(SyscallRegisters& regs)
    { 
        using namespace np::Syscall;
        bool advancedInfo = (regs.arg0 != 0);
        DeviceType devType = static_cast<np::Syscall::DeviceType>(regs.arg1);
        
        if (advancedInfo)
        {
            Log("Getting advanced device info is not supported currently.", LogSeverity::Error);
            regs.id = (uint64_t)GetDeviceError::FeatureNotAvailable;
            return;
        }

        switch (devType)
        {
        case DeviceType::Framebuffer:
            FetchBasicFramebufferInfo(regs);
            break;
        
        default:
            Log("Unknown device type for GetPrimaryDeviceInfo().", LogSeverity::Warning);
            regs.id = (uint64_t)GetDeviceError::UnknownDeviceType;
            break;
        }
    }

    void GetDevicesOfType(SyscallRegisters& regs)
    { regs.id = 1; }

    void GetDeviceInfo(SyscallRegisters& regs)
    { regs.id = 1; }
}
