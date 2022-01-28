#include <SyscallDispatch.h>
#include <Syscalls.h>
#include <SyscallEnums.h>

#include <drivers/DriverManager.h>
#include <devices/pci/BochsGraphicsAdaptor.h>

namespace Kernel
{
    StoredRegisters* DispatchSyscall(StoredRegisters* regs)
    {
        //TODO: enable interrupts, send EOI and mark this iret frame as having been handled (but not returned from). Lets us being pre-empted within syscalls.
        //or
        //we could modify iret frame to return to kernel ss:rsp and cs:rip of interrupt handling code. This would 'just work' with existing scheduling stuff.
        
        using np::Syscall::SyscallId;
        SyscallId attemptedId = static_cast<SyscallId>(regs->rax);
        switch (attemptedId)
        {
        case SyscallId::LoopbackTest:
            regs->rax = 0;
            break;
        case SyscallId::GetPrimaryFramebuffer:
        {
            //This is a big hack for now, until we have a device manager: we know we're going to have access to a bochs VGA driver, use it directly.
            regs->rax = 0;
            
            using namespace Drivers;
            uint8_t machNameBytes[] = { 0x11, 0x11, 0x34, 0x12 };
            DriverMachineName machName;
            machName.length = 4;
            machName.name = machNameBytes;
            sl::Opt<DriverManifest*> maybeBgaManifest = DriverManager::Global()->FindDriver(DriverSubsytem::PCI, machName);
            if (!maybeBgaManifest.HasValue())
            {
                regs->rax = 1;
                break;
            }
            
            using namespace Devices::Pci;
            BochsGraphicsDriver* bgaGpu = reinterpret_cast<BochsGraphicsDriver*>(DriverManager::Global()->GetDriverInstance(*maybeBgaManifest, 0));
            BochsFramebuffer* primaryFramebuffer = static_cast<BochsFramebuffer*>(bgaGpu->GetAdaptor()->GetFramebuffer(0));

            regs->rdi = primaryFramebuffer->GetAddress().Value().raw;
            Devices::Interfaces::FramebufferModeset modeset = primaryFramebuffer->GetCurrentMode();
            regs->rsi = modeset.width | (modeset.height << 32);
            regs->rdx = modeset.bitsPerPixel | (modeset.width * modeset.bitsPerPixel);
            regs->rcx = 0x00 | (8 << 8) | (16 << 16) | (24 << 24) | (0xFFFF'FFFFl << 32);
            break;
        }

        default:
            regs->rax = 0x404; //couldnt find matching system call
            break;
        }

        return regs;
    }
}
