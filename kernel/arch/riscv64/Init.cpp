#include <arch/riscv64/Interrupts.h>
#include <arch/riscv64/Aia.h>
#include <arch/Platform.h>
#include <boot/CommonInit.h>
#include <config/AcpiTables.h>
#include <config/DeviceTree.h>
#include <debug/Log.h>
#include <NativePtr.h>
#include <ArchHints.h>
#include <NanoPrintf.h>

namespace Npk
{
#define NPK_RV64_ASSUME_UART 0x10000000
#ifdef NPK_RV64_ASSUME_UART
    sl::NativePtr uartRegs;

    static void UartWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
        {
            while ((uartRegs.Offset(5).Read<uint8_t>() & (1 << 5)) == 0)
                sl::HintSpinloop();
            uartRegs.Write<uint8_t>(text[i]);
        }
    }

    Debug::LogOutput uartOutput
    {
        .Write = UartWrite,
        .BeginPanic = nullptr,
    };
#endif

    void ArchKernelEntry()
    {
        asm volatile("mv tp, zero");
        asm volatile("csrw sscratch, zero");
    }

    void ArchLateKernelEntry()
    {
#ifdef NPK_RV64_ASSUME_UART
        uartRegs = hhdmBase + NPK_RV64_ASSUME_UART;
        Debug::AddLogOutput(&uartOutput);
#endif

        ASSERT(InitAia(), "AIA is required for riscv platform");
    }

    static void StoreIsaString()
    {
        using namespace Config;

        auto maybeMadt = FindAcpiTable(SigMadt);
        auto maybeRhct = FindAcpiTable(SigRhct);
        if (maybeMadt.HasValue() && maybeRhct.HasValue())
        {
            uint32_t hartAcpiId = -1;

            //ok so this is way more work it has to be (classic riscv moment). First we need to
            //find the acpi processor id for this hart.
            sl::CNativePtr madtEntries = static_cast<const Madt*>(*maybeMadt)->sources;
            while (madtEntries.raw < (uintptr_t)*maybeMadt + maybeMadt.Value()->length)
            {
                auto source = madtEntries.As<const MadtSource>();
                if (source->type == MadtSourceType::RvLocalController)
                {
                    auto rvIntrController = madtEntries.As<MadtSources::RvLocalController>();
                    if (rvIntrController->hartId == CoreLocal().id)
                    {
                        hartAcpiId = rvIntrController->acpiProcessorId;
                        break;
                    }
                }
                madtEntries.raw += source->length;
            }
            ASSERT(hartAcpiId != -1, "Could not find current hart in MADT.");

            //now with the acpi processor id, we can find the matching interrupt controller
            //entry in the RHCT.
            auto rhct = static_cast<const Rhct*>(*maybeRhct);
            const RhctNodes::HartInfoNode* hartInfo = nullptr;
            while (true)
            {
                auto maybeNode = FindRhctNode(rhct, RhctNodeType::HartInfo, hartInfo);
                if (!maybeNode)
                {
                    hartInfo = nullptr;
                    break;
                }
                
                hartInfo = static_cast<const RhctNodes::HartInfoNode*>(*maybeNode);
                if (hartInfo->acpiProcessorId != hartAcpiId)
                    continue;
                break;
            }
            ASSERT_(hartInfo != nullptr);

            bool foundIsaString = false;
            sl::CNativePtr rhctAccess = rhct;
            for (size_t i = 0; i < hartInfo->offsetCount; i++)
            {
                auto node = rhctAccess.Offset(hartInfo->offsets[i]).As<const RhctNode>();
                if (node->type != RhctNodeType::IsaString)
                    continue;

                foundIsaString = true;
                auto isaNode = static_cast<const RhctNodes::IsaStringNode*>(node);
                sl::StringSpan isaString { (const char*)isaNode->str, 0 };

                static_cast<ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig])->isaString = isaString;
                Log("Found isa string via acpi: %.*s", LogLevel::Info, (int)isaString.Size(),
                    isaString.Begin());
                break;
            }

            ASSERT_(foundIsaString);
            return;
        }
        else if (DeviceTree::Global().Available())
        {
            constexpr size_t MaxNodeNameLen = 64;

            char nodeName[MaxNodeNameLen];
            const size_t nodeNameLen = npf_snprintf(nodeName, MaxNodeNameLen, "/cpus/cpu@%lu", CoreLocal().id);
            ASSERT_(nodeNameLen < MaxNodeNameLen);

            const DtNode* cpuNode = DeviceTree::Global().Find({ nodeName, nodeNameLen });
            ASSERT_(cpuNode != nullptr);
            DtProp* isaProp = cpuNode->FindProp("riscv,isa");
            ASSERT_(isaProp != nullptr);
            
            auto isaString = isaProp->ReadString(0);
            static_cast<ArchConfig*>(CoreLocal()[LocalPtr::ArchConfig])->isaString = isaString;
            Log("Found isa string in device tree: %.*s", LogLevel::Info, (int)isaString.Size(), 
                isaString.Begin());
            return;
        }

        ASSERT(false, "No known way to get ISA string.");
    }

    void ArchInitCore(size_t myId)
    {
        LoadStvec();
        ClearCsrBits("sstatus", 1 << 19); //enable MXR

        CoreLocalInfo* clb = new CoreLocalInfo();
        asm volatile("mv tp, %0" :: "r"(clb));
        CoreLocal()[LocalPtr::ArchConfig] = new ArchConfig();
        clb->id = myId;
        clb->runLevel = RunLevel::Dpc;

        StoreIsaString();

        //TODO: extended state init
    }

    void ArchThreadedInit()
    {}
}
