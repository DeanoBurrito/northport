#include <arch/riscv64/IntControllers.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <memory/Vmm.h>
#include <containers/LinkedList.h>

//These are fairly new CSRs, and not assemblers support using their mnemonics yet. 
#define CSR_SI_SELECT "0x150"
#define CSR_SI_REG "0x151"

namespace Npk
{
    enum class AiaReg
    {
        Delivery = 0x70,
        Threshold = 0x72,
        PendingBase = 0x80,
        EnableBase = 0xC0,
    };

    struct ImsicPage
    {
        uintptr_t physAddr;
        sl::NativePtr mmio;
        size_t coreId;
    };
    
    struct Aplic
    {
        sl::NativePtr mmio;
    };

    sl::LinkedList<ImsicPage> imsicPages;
    Aplic* aplic;

    bool DiscoverImsic()
    {
        using namespace Config;

        DtNode prev;
        while (true)
        {
            auto maybeFound = DeviceTree::Global().GetCompatibleNode("riscv,imsics", prev);
            if (!maybeFound)
                break;
            prev= *maybeFound;

            auto maybeExtInts = maybeFound->GetProp("interrupts-extended");
            if (!maybeExtInts)
                continue;
            
            //cell A is the phandle of the target (only 1 cell in this case), and
            //cell B is the vector triggered on the target (also only 1 cell).
            const size_t pairCount = maybeExtInts->ReadPairs(1, 1, nullptr);
            if (pairCount < 1)
                continue;
            DtPair pairs[pairCount];
            maybeExtInts->ReadPairs(1, 1, pairs);

            auto regsProp = prev.GetProp("reg");
            if (!regsProp)
                continue;
            const size_t regsCount = regsProp->ReadRegs(prev, nullptr);
            DtReg regs[regsCount];
            regsProp->ReadRegs(prev, regs);

            const uintptr_t accessWindow = VMM::Kernel().Alloc(regs[0].length, regs[0].length,
                VmFlags::Write | VmFlags::Mmio)->base;

            for (size_t i = 0; i < pairCount; i++)
            {
                if (pairs[i].b != 9)
                    continue; //we only care about triggering interrupt 9 (s-mode external)

                auto coreNode = DeviceTree::Global().GetByPHandle(pairs[i].a);
                if (!coreNode)
                    continue;

                ImsicPage& page = imsicPages.EmplaceBack();
                page.physAddr = regs[0].base + (i * 0x1000); //IMSIC pages are 4K in size
                page.mmio = accessWindow + (i * 0x1000);
                //TODO: phandle gives us the int-controller node that is a child of the cpu node we want.
                //we'll need a way to get a node's parent. Time to revist DTB parser?
                
                Log("Imsic s-mode page added: core=%lu, phys=0x%lx.", LogLevel::Verbose,
                    page.coreId, page.physAddr);
            }
        }

        return imsicPages.Size() > 0;
    }

    bool DiscoverAplic()
    {
        using namespace Config;
        DtNode prev;
        while (true)
        {
            auto maybeFound = DeviceTree::Global().GetCompatibleNode("riscv,aplic", prev);
            if (!maybeFound)
                break;
        }

        return true;
    }

    bool DiscoverPlic()
    {
        return false;
    }

    bool DiscoverAclint()
    {
        return false;
    }

    void InitIntControllers()
    {
        //ah yes, this new architecture is much simpler compared to others - gj.
        if (DiscoverImsic())
        {
            WriteCsr(CSR_SI_SELECT, (uint64_t)AiaReg::Delivery);
            WriteCsr(CSR_SI_REG, 1); //enable delivery from the imsics.
            WriteCsr(CSR_SI_SELECT, (uint64_t)AiaReg::Threshold);
            WriteCsr(CSR_SI_REG, 0); //disable threshold functionality - all levels are heard.
            Log("Using IMSIC as external interrupt controller, threshold=0.", LogLevel::Info);

            if (DiscoverAplic())
                Log("APLIC is supporting interrupt controller for IMSIC.", LogLevel::Info);
            else
                Log("No APLIC present, only MSIs supported on this system.", LogLevel::Info);
        }
        else if (DiscoverAplic())
        {}
        else if (DiscoverPlic())
        {}
        else
            Log("Unknown external interrupt controller.", LogLevel::Warning);

        if (DiscoverAclint())
        {}
        else
            Log("ACLINT not available, SBI will be used for IPIs", LogLevel::Info);
    }

    sl::Opt<size_t> HandleExternalInterrupt()
    {
        //if we're using the AIA (imsic and/or aplic) use stopi
        if (imsicPages.Size() > 0 || aplic != nullptr)
        {}
        else
        {} //access plic registers

        return {};
    }
}

