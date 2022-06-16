#include <syscalls/Dispatch.h>
#include <SyscallEnums.h>
#include <devices/DeviceManager.h>
#include <devices/interfaces/GenericFramebuffer.h>
#include <scheduling/Thread.h>
#include <Log.h>

#define CHECK_DEVICE(expectedType) \
using namespace Devices; \
if (!maybeDevice) \
{ \
    regs.id = (uint64_t)np::Syscall::DeviceError::NoPrimaryDevice; \
    return; \
} \
if (maybeDevice.Value()->Type() != DeviceType::expectedType) \
{ \
    regs.id = (uint64_t)np::Syscall::DeviceError::MismatchedDeviceType; \
    return; \
} 

namespace Kernel::Syscalls
{
    void FetchGraphicsAdaptorInfo(SyscallRegisters& regs, sl::Opt<Devices::GenericDevice*> maybeDevice)
    {
        CHECK_DEVICE(GraphicsAdaptor)
        regs.arg0 = maybeDevice.Value()->GetId();
    }
    
    void FetchFramebufferInfo(SyscallRegisters& regs, sl::Opt<Devices::GenericDevice*> maybeDevice)
    {
        CHECK_DEVICE(GraphicsFramebuffer)

        const auto* fb = static_cast<Interfaces::GenericFramebuffer*>(maybeDevice.Value());
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
        regs.arg2 = Scheduling::Thread::Current()->Parent()->VMM()->AllocMmioRange(
            EnsureLowerHalfAddr(fb->GetAddress()->raw),
            (modeset.width * modeset.bitsPerPixel / 8) * modeset.height, 
            MFlags::AllowWrites | MFlags::UserAccessible | MFlags::SystemRegion).base;
    }

    void FetchKeyboardInfo(SyscallRegisters& regs, sl::Opt<Devices::GenericDevice*> maybeDevice)
    {
        CHECK_DEVICE(Keyboard)
    }

    void FetchMouseInfo(SyscallRegisters& regs, sl::Opt<Devices::GenericDevice*> maybeDevice)
    {
        CHECK_DEVICE(Mouse);
    }
    
    void GetPrimaryDeviceInfo(SyscallRegisters& regs)
    { 
        using namespace np::Syscall;

        //NOTE: we are converting from a syscall device type to an inbuilt device type.
        Devices::DeviceType devType = static_cast<Devices::DeviceType>(regs.arg0);
        auto maybeDevice = Devices::DeviceManager::Global()->GetPrimaryDevice(devType);

        switch (devType)
        {
        case Devices::DeviceType::GraphicsAdaptor:
            FetchGraphicsAdaptorInfo(regs, maybeDevice);
            break;

        case Devices::DeviceType::GraphicsFramebuffer:
            FetchFramebufferInfo(regs, maybeDevice);
            break;
        
        case Devices::DeviceType::Keyboard:
            FetchKeyboardInfo(regs, maybeDevice);
            break;
        
        case Devices::DeviceType::Mouse:
            FetchMouseInfo(regs, maybeDevice);
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

    void EnableDeviceEvents(SyscallRegisters& regs)
    {
        Devices::DeviceManager::Global()->SubscribeToDeviceEvents(regs.arg0);
    }

    void DisableDeviceEvents(SyscallRegisters& regs)
    {
        Devices::DeviceManager::Global()->UnsubscribeFromDeviceEvents(regs.arg0);
    }

    void GetAggregateId(SyscallRegisters& regs)
    {
        //NOTE: we are converting from a syscall device type to an inbuilt device type.
        Devices::DeviceType devType = static_cast<Devices::DeviceType>(regs.arg0);

        if (devType == Devices::DeviceType::Keyboard)
            regs.arg0 = Devices::DeviceManager::Global()->GetAggregateId(Devices::DeviceType::Keyboard);
        else if (devType == Devices::DeviceType::Mouse)
            regs.arg0 = Devices::DeviceManager::Global()->GetAggregateId(Devices::DeviceType::Mouse);
        else
            regs.id = (uint64_t)np::Syscall::DeviceError::FeatureNotAvailable;
    }
}
