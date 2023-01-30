#include <drivers/builtin/Nvme.h>
#include <drivers/InitTags.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <memory/Pmm.h>
#include <tasking/Thread.h>
#include <Maths.h>

namespace Npk::Drivers
{
    void NvmeMain(void* arg)
    {
        NvmeController driver;

        auto pciAddrTag = FindTag(arg, InitTagType::Pci);
        if (!pciAddrTag || !driver.Init(static_cast<PciInitTag*>(*pciAddrTag)->address))
        {
            Log("NVMe Controller init failed.", LogLevel::Error);
            Tasking::Thread::Current().Exit(1);
        }

        Log("NVMe init done.", LogLevel::Debug);
        while (true)
            HintSpinloop();
    }

    void NvmeController::Enable(bool yes)
    {
        auto& props = *propsAccess->As<volatile ControllerProps>();

        if (yes)
        {
            //Enable controller: the controller is not required to preserve any state
            //once the enable bit is set, so we should write all our settings at once.
            //We also leave a number of bits zeroed as well, like the arbitration mechanism
            //and page size (defaulting to 4KiB), and we select the NVM command set (0b000).

            uint32_t config = 1; //enable bit.
            config |= 6 << 16; //IOSQES: io submission queue entry size as a power of 2. 6 ^ 2 = 64 bytes.
            config |= 4 << 20; //IOCQES: completion queue entry size as power of 2. 4 ^ 2 = 16 bytes.
            props.config = config;

            while ((props.status & 1) == 0)
                HintSpinloop(); //wait for controller to signal it's initialized
            
            ASSERT((props.status & 0b10) == 0, "NVMe controller fatal error");
            Log("NVMe controller enabled.", LogLevel::Verbose);
        }
        else
        {
            //disable controller
            props.config &= ~(uint32_t)1;
            while ((props.status & 1) == 1)
                HintSpinloop();
            Log("NVMe controller disabled.", LogLevel::Verbose);
        }
    }

    bool NvmeController::CreateAdminQueue(size_t entries)
    {
        VALIDATE(queues.Size() == 0, false, "NVMe controller already has admin queue.");

        queuesLock.Lock();
        NvmeQ& q = queues.EmplaceAt(0);
        sl::ScopedLock qLock(q.lock);
        queuesLock.Unlock();

        sl::NativePtr doorbellBase(propsAccess->raw + 0x1000);
        q.entries = entries;
        q.nextCmdId = 1;

        const size_t sqBytes = entries * sizeof(SqEntry);
        const uintptr_t sqPhys = PMM::Global().Alloc(sl::AlignUp(sqBytes, PageSize));
        if (sqPhys == 0)
            return false;
        q.sq = sl::NativePtr(sqPhys + hhdmBase).As<volatile SqEntry>();
        q.sqDoorbell = doorbellBase.As<volatile uint32_t>();

        const size_t cqBytes = entries * sizeof(CqEntry);
        const uintptr_t cqPhys = PMM::Global().Alloc(sl::AlignUp(cqBytes, PageSize));
        if (cqPhys == 0)
            return false;
        q.cq = sl::NativePtr(cqPhys + hhdmBase).As<CqEntry>();
        q.cqDoorbell = doorbellBase.As<uint32_t>(doorbellStride);
        
        Log("NVMe admin queue created: sq=0x%lx, cq=0x%lx, entries=0x%lu", LogLevel::Verbose,
            (uintptr_t)sqPhys, (uintptr_t)cqPhys, entries);
        return true;
    }

    bool NvmeController::DestroyAdminQueue()
    {
        VALIDATE(queues.Size() == 1, false, "Cannot remove admin queue, other queues still exist.");

        Enable(false);

        queuesLock.Lock();
        NvmeQ q = queues.PopBack();
        queuesLock.Unlock();
        
        const size_t sqPages = sl::AlignUp(q.entries * sizeof(SqEntry), PageSize);
        PMM::Global().Free((uintptr_t)q.sq - hhdmBase, sqPages);

        const size_t cqPages = sl::AlignUp(q.entries * sizeof(CqEntry), PageSize);
        PMM::Global().Free((uintptr_t)q.cq, cqPages);

        auto& props = *propsAccess->As<volatile ControllerProps>();
        props.aqa = 0;
        props.asq = 0;
        props.acq = 0;
        
        ASSERT_UNREACHABLE()
    }

    bool NvmeController::CreateIoQueue(size_t index)
    {
        ASSERT_UNREACHABLE()
    }

    bool NvmeController::DestroyIoQueue(size_t index)
    {
        ASSERT_UNREACHABLE()
    }

    bool NvmeController::Init(Devices::PciAddress pciAddr)
    {
        //Note that during init we read a few fields that are reserved in earlier
        //versions of the spec. This is fine as the spec states reserved fields
        //read as zero, and writes are ignored.
        addr = pciAddr;

        addr.MemorySpaceEnable(true);
        addr.BusMastering(true);
        addr.InterruptDisable(true);

        //for PCIe we get the address of the properties table from BAR0.
        const Devices::PciBar bar0 = addr.ReadBar(0);
        propsAccess = VmObject(bar0.size, bar0.address, VmFlags::Mmio | VmFlags::Write);

        //disable controller for now, and assert some expectations
        Enable(false);
        auto& props = *propsAccess->As<volatile ControllerProps>();
        Log("NVMe controller capabilities: 0x%lx", LogLevel::Verbose, props.capabilities);

        //check support for NVM command set
        VALIDATE(((props.capabilities >> 37) & 1) != 0, false, "NVMe controller does not support NVM command set.");
        //check controller supports native page size
        const size_t minPageSize = 0x1000 << ((props.capabilities >> 48) & 0xF);
        const size_t maxPageSize = 0x1000 << ((props.capabilities >> 52) & 0xF);
        VALIDATE(PageSize >= minPageSize && PageSize <= maxPageSize, false, 
            "NVMe controller does not support native page size.");

        //gather some data for later
        doorbellStride = 4 << ((props.capabilities >> 32) & 0xF);
        ioQueueMaxSize = (props.capabilities & 0xFFFF) + 1;

        const size_t adminEntries = sl::Min(PageSize / sizeof(SqEntry), 4096ul);
        VALIDATE(CreateAdminQueue(adminEntries), false, "CreateAdminQueue()");
        props.aqa = adminEntries | (adminEntries << 16);
        props.acq = reinterpret_cast<uintptr_t>(queues[0].cq) - hhdmBase;
        props.asq = reinterpret_cast<uintptr_t>(queues[0].sq) - hhdmBase;

        //mask interrupts before re-enabling, since msi(-x) isn't set up yet.
        props.interruptMaskSet = ~(uint32_t)0;

        //re-enable controller so we can begin submitting commands to admin queues
        Enable(true);
        Log("NVMe config: pageSize=0x%lx (0x%lx - 0x%lx), qMaxSize=%lu", 
            LogLevel::Verbose, PageSize, minPageSize, maxPageSize, ioQueueMaxSize);
        Log("NVMe version: %u.%u.%u", LogLevel::Verbose, 
        props.version >> 16, (props.version >> 8) & 0xF, props.version & 0xF);

        return true;
    }

    bool NvmeController::Deinit()
    {
        ASSERT_UNREACHABLE();
    }
}
