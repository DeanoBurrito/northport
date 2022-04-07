#include <arch/x86_64/ApBoot.h>
#include <acpi/AcpiTables.h>
#include <devices/LApic.h>
#include <memory/Paging.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>

#define SMP_FLAG_XD (1 << 0)
#define SMP_FLAG_LA57 (1 << 1)

asm("\
ApTrampolineBegin: \n\
    .incbin \"build/arch/x86_64/ApTrampoline.s.o\" \n\
ApTrampolineEnd: \n\
");

extern "C"
{
    extern uint8_t ApTrampolineBegin[];
    extern uint8_t ApTrampolineEnd[];
}

namespace Kernel
{
    SmpInfo* BootAPs()
    {
        using Devices::LApic;

        //we'll need to identity map the trampoline code, data and lapic mmio register pages
        Memory::PageTableManager::Current()->MapRange(AP_BOOTSTRAP_BASE, AP_BOOTSTRAP_BASE, 2, Memory::MemoryMapFlags::AllowExecute | Memory::MemoryMapFlags::AllowWrites);
        const size_t lapicBase = CPU::ReadMsr(MSR_APIC_BASE) & ~(size_t)0xFFF;
        Memory::PageTableManager::Current()->MapMemory(lapicBase, lapicBase, Memory::MemoryMapFlags::AllowWrites);
        
        //zero space for code, data and config block
        sl::memset((void*)AP_BOOTSTRAP_BASE, 0, PAGE_FRAME_SIZE * 0);

        //set up temporary gdt
        sl::NativePtr gdtBase = sl::NativePtr(AP_BOOTSTRAP_BASE + PAGE_FRAME_SIZE);
        uint64_t* gdt = gdtBase.As<uint64_t>();
        gdt[0] = 0;                     //0x00: null
        gdt[1] = 0x0000'9a00'0000'ffff; //0x08: 16-bit code
        gdt[2] = 0x0000'9300'0000'ffff; //0x10: 16-bit data
        gdt[3] = 0x00cf'9a00'0000'ffff; //0x18: 32-bit code
        gdt[4] = 0x00cf'9300'0000'ffff; //0x20: 32-bit data
        gdt[5] = 0x00af'9b00'0000'ffff; //0x28: 64-bit code
        gdt[6] = 0x00af'9300'0000'ffff; //0x30: 64-bit data


        //prepare bootstrap data
        SmpInfo* smpInfo = gdtBase.As<SmpInfo>(0x100);
        smpInfo->bspApicId = LApic::Local()->GetId();
        smpInfo->bootstrapDetails.cr3 = ReadCR3();
        smpInfo->bootstrapDetails.gdtr = (((uint64_t)gdt) << 16) | (uint16_t)(8 * 7) - 1;
        if (CPU::FeatureSupported(CpuFeature::ExecuteDisable))
            smpInfo->bootstrapDetails.flags |= SMP_FLAG_XD;
        if ((ReadCR4() & (1 << 12)) != 0)
            smpInfo->bootstrapDetails.flags |= SMP_FLAG_LA57;

        //populate entry for bsp, rest of the details wont do anything
        smpInfo->cores[smpInfo->bspApicId].apicId = smpInfo->bspApicId;

        //copy the trampoline to where it expects to be
        const sl::NativePtr trampolineBase(AP_BOOTSTRAP_BASE);
        const size_t trampolineSize = (size_t)ApTrampolineEnd - (size_t)ApTrampolineBegin;
        sl::memcopy(ApTrampolineBegin, trampolineBase.ptr, trampolineSize);

        //traverse the MADT for any lapic entries, and try to initialize those cores
        ACPI::MADT* madt = static_cast<ACPI::MADT*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MADT));
        if (madt == nullptr)
        {
            Log("Unable to boot APs, could not find MADT.", LogSeverity::Warning);
            return smpInfo;
        }

        //expands out to 0x44'000 when SIPI is received on the remote core, which is where execution starts.
        const uint8_t coreStartupVector = 0x44;
        size_t highestCoreId = smpInfo->bspApicId;

        const size_t madtEnd = (size_t)madt + madt->length;
        sl::NativePtr scan = madt->controllerEntries;
        while (scan.raw < madtEnd)
        {
            switch (scan.As<ACPI::MadtEntry>()->type)
            {
            case ACPI::MadtEntryType::LocalApic:
                {
                    ACPI::MadtEntries::LocalApicEntry* apicEntry = scan.As<ACPI::MadtEntries::LocalApicEntry>();

                    //stash info and try to init core
                    smpInfo->cores[apicEntry->apicId].apicId = apicEntry->apicId;
                    smpInfo->cores[apicEntry->apicId].acpiProcessorId = apicEntry->acpiProcessorId;

                    if (apicEntry->apicId > highestCoreId)
                        highestCoreId = apicEntry->apicId;

                    if (apicEntry->apicId == smpInfo->bspApicId)
                        break; //we dont want try reset the bsp, that would be... interesting.

                    Logf("Attempting to init remote core %u", LogSeverity::Verbose, apicEntry->apicId);
                    LApic::Local()->SendStartup(apicEntry->apicId, coreStartupVector);
                    break;
                }

            default:
                break;
            }

            scan.raw += scan.As<ACPI::MadtEntry>()->length;
        }
        
        //mark any duplicate 'id=0' entries (i.e. not filled in) entries are invalid
        highestCoreId++;
        for (size_t i = 1; i < highestCoreId; i++)
        {
            if (smpInfo->cores[i].apicId == 0)
                smpInfo->cores[i].apicId = AP_BOOT_APIC_ID_INVALID;
        }

        smpInfo->cores[highestCoreId].apicId = AP_BOOT_APIC_ID_END;
        return smpInfo;
    }

    void ApBootCleanup()
    {
        //TODO: free the physical pages used for the AP bootstrap, and the low memory pages we mapped above (data + code + lapic regs)
    }
}
