#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <devices/DeviceManager.h>
#include <scheduling/Thread.h>
#include <Log.h>

#define GET_DEVICE(regs, deviceId, expectedType) \
using namespace Devices; \
auto maybeDevice = DeviceManager::Global()->GetDevice(deviceId); \
if (!maybeDevice) \
{ \
    regs.id = (uint64_t)np::Syscall::DeviceError::InvalidDeviceId; \
    return; \
} \
if (maybeDevice.Value()->Type() != DeviceType::expectedType) \
{ \
    regs.id = (uint64_t)np::Syscall::DeviceError::MismatchedDeviceType; \
    return; \
} 

namespace Kernel::Syscalls
{
    void FetchGraphicsAdaptorInfo(SyscallRegisters& regs, size_t deviceId)
    {
        GET_DEVICE(regs, deviceId, GraphicsAdaptor)

        regs.arg0 = deviceId;
    }
    
    void FetchFramebufferInfo(SyscallRegisters& regs, size_t deviceId)
    {
        GET_DEVICE(regs, deviceId, GraphicsFramebuffer)

        const auto* fb = static_cast<Devices::Interfaces::GenericFramebuffer*>(*maybeDevice);
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

        //if the requesting thread is running in userspace, we'll need to map the framebuffer into the lower half for them
        if (sl::EnumHasFlag(Scheduling::Thread::Current()->Flags(), Scheduling::ThreadFlags::KernelMode))
            return;

        using MFlags = Memory::MemoryMapFlags;
        regs.arg2 = Scheduling::Thread::Current()->Parent()->VMM()->AllocateRange(
            EnsureLowerHalfAddr(fb->GetAddress()->raw),
            (modeset.width * modeset.bitsPerPixel / 8) * modeset.height, 
            MFlags::AllowWrites | MFlags::UserAccessible | MFlags::SystemRegion).raw;
    }

    void FetchKeyboardInfo(SyscallRegisters& regs, size_t deviceId)
    {
        GET_DEVICE(regs, deviceId, Keyboard)
        regs.arg0 = deviceId;
        regs.arg1 = DeviceManager::Global()->GetAggregateId(DeviceType::Keyboard);
    }

    void FetchMouseInfo(SyscallRegisters& regs, size_t deviceId)
    {
        GET_DEVICE(regs, deviceId, Mouse)
        regs.arg0 = deviceId;
        regs.arg1 = DeviceManager::Global()->GetAggregateId(DeviceType::Mouse);
    }
    
    void GetPrimaryDeviceInfo(SyscallRegisters& regs)
    { 
        using namespace np::Syscall;
        //NOTE: we are converting from a syscall device type to an inbuilt device type.
        Devices::DeviceType devType = static_cast<Devices::DeviceType>(regs.arg0);
        auto maybeDevice = Devices::DeviceManager::Global()->GetPrimaryDevice(devType);
        if (!maybeDevice)
        {
            regs.id = (uint64_t)DeviceError::NoPrimaryDevice;
            return;
        }

        switch (devType)
        {
        case Devices::DeviceType::GraphicsAdaptor:
            FetchGraphicsAdaptorInfo(regs, maybeDevice.Value()->GetId());
            break;

        case Devices::DeviceType::GraphicsFramebuffer:
            FetchFramebufferInfo(regs, maybeDevice.Value()->GetId());
            break;
        
        case Devices::DeviceType::Keyboard:
            FetchKeyboardInfo(regs, maybeDevice.Value()->GetId());
            break;
        
        case Devices::DeviceType::Mouse:
            FetchMouseInfo(regs, maybeDevice.Value()->GetId());
            break;
        
        default:
            Log("Unknown device type for GetPrimaryDeviceInfo().", LogSeverity::Warning);
            regs.id = (uint64_t)DeviceError::UnknownDeviceType;
            break;
        }
    }

    void GetDevicesOfType(SyscallRegisters& regs)
    { regs.id = 1; }

    void GetDeviceInfo(SyscallRegisters& regs)
    { regs.id = 1; }

    
}
