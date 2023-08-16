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
        DtNode* prev = nullptr;
        while (true)
        {
            DtNode* found = DeviceTree::Global().FindCompatible("riscv,imsics", prev);
            if (found == nullptr)
                break;
            prev = found;

            DtProp* extInts = found->FindProp("interrupts-extended");
            if (extInts == nullptr)
                continue;
            
            //cell A is the phandle of the target (only 1 cell in this case), and
            //cell B is the vector triggered on the target (also only 1 cell).
            DtPair layout { 1, 1 };
            const size_t pairCount = extInts->ReadPairs(layout, {});
            if (pairCount < 1)
                continue;
            DtPair pairs[pairCount];
            extInts->ReadPairs(layout, { pairs, pairCount });

            DtProp* regsProp = found->FindProp("reg");
            if (regsProp == nullptr)
                continue;
            const size_t regsCount = regsProp->ReadRegs({});
            DtPair regs[regsCount];
            regsProp->ReadRegs({ regs, regsCount });

            //NOTE: we dont create a VMO for this since we never intend to free the addr space
            const uintptr_t access = VMM::Kernel().Alloc(regs[0][0], regs[0][1],
                VmFlags::Write | VmFlags::Mmio)->base;

            for (size_t i = 0; i < pairCount; i++)
            {
                if (pairs[i][1] != 9)
                    continue; //we only care about triggering interrupt 9 (s-mode external)

                DtNode* cpuNode = DeviceTree::Global().FindPHandle(pairs[i][0]);
                if (cpuNode == nullptr)
                    continue;

                ImsicPage& page = imsicPages.EmplaceBack();
                page.physAddr = regs[0][0] + (i * 0x1000); //IMSIC pages are 4K in size
                page.mmio = access + (i * 0x1000);
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
        DtNode* prev = nullptr;
        while (true)
        {
            DtNode* found = DeviceTree::Global().FindCompatible("riscv,aploc", prev);
            if (found == nullptr)
                break;
            prev = found;

            //TODO: init new aplic
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

