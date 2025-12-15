#include <EntryPrivate.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <AcpiTypes.hpp>
#include <UnitConverter.hpp>
#include <Memory.hpp>

namespace Npk
{
    static Paddr configRootPtr;
    static sl::Opt<ConfigRootType> configRootType;

    void SetConfigRoot(const Loader::LoadState& loaderState)
    {
        NPK_ASSERT(!configRootType.HasValue());

        if (loaderState.rsdp.HasValue())
        {
            configRootType = ConfigRootType::Rsdp;
            configRootPtr = *loaderState.rsdp;
        }
        else if (loaderState.fdt.HasValue())
        {
            configRootType = ConfigRootType::Fdt;
            configRootPtr = *loaderState.fdt;
        }

        constexpr const char* TypeStrs[] = { "rsdp", "fdt", "bootinfo" };
        const char* typeStr = TypeStrs[static_cast<size_t>(*configRootType)];

        Log("Config root pointer set: %s @ 0x%tx", LogLevel::Info, 
            typeStr, configRootPtr);
    }

    sl::Opt<Paddr> GetConfigRoot(ConfigRootType type)
    {
        if (configRootType.HasValue() && *configRootType == type)
            return configRootPtr;
        return {};
    }

    struct AcpiTableAccess
    {
        char signature[4];
        Paddr paddr;
        void* vaddr;
    };

    static sl::Span<AcpiTableAccess> acpiTables;

    void TryMapAcpiTables(uintptr_t& virtBase)
    {
        NPK_ASSERT(acpiTables.Empty());

        auto rsdpPhys = GetConfigRoot(ConfigRootType::Rsdp);
        if (!rsdpPhys.HasValue())
            return;

        char rsdpBuff[sizeof(sl::Rsdp)];
        NPK_CHECK(CopyFromPhysical(*rsdpPhys, rsdpBuff) == sizeof(sl::Rsdp), );
        auto rsdp = reinterpret_cast<sl::Rsdp*>(rsdpBuff);

        Paddr ptrsBase;
        size_t ptrsCount;
        size_t ptrSize;
        if (rsdp->revision == 0 || rsdp->xsdt == 0)
        {
            char rsdtBuff[sizeof(sl::Rsdt)];
            NPK_CHECK(sizeof(sl::Rsdt) == CopyFromPhysical(rsdp->rsdt, rsdtBuff), );
            auto rsdt = reinterpret_cast<sl::Rsdt*>(rsdtBuff);

            ptrsCount = (rsdt->length - sizeof(sl::Sdt)) / sizeof(uint32_t);
            ptrSize = 4;
            ptrsBase = rsdp->rsdt + sizeof(sl::Sdt);
        }
        else
        {
            char xsdtBuff[sizeof(sl::Xsdt)];
            NPK_CHECK(sizeof(sl::Xsdt) == CopyFromPhysical(rsdp->xsdt, xsdtBuff), );
            auto xsdt = reinterpret_cast<sl::Xsdt*>(xsdtBuff);

            ptrsCount = (xsdt->length - sizeof(sl::Sdt)) / sizeof(uint64_t);
            ptrSize = 8;
            ptrsBase = rsdp->xsdt + sizeof(sl::Sdt);
        }
        Log("Acpi sdt config: %s has %zux %zu-byte addresses.", LogLevel::Verbose,
            ptrSize == 4 ? "rsdt" : "xsdt", ptrsCount, ptrSize);

        acpiTables = sl::Span<AcpiTableAccess>(reinterpret_cast<AcpiTableAccess*>(virtBase), ptrsCount);
        for (size_t i = 0; i < ptrsCount * sizeof(AcpiTableAccess); i += PageSize(), virtBase += PageSize())
        {
            const auto page = AllocPage(false);
            const auto status = SetKernelMap(virtBase, LookupPagePaddr(page), 
                VmFlag::Write);

            NPK_ASSERT(status == VmStatus::Success);
        }

        for (size_t i = 0; i < ptrsCount; i++)
        {
            const Paddr ptrPaddr = ptrsBase + ptrSize * i;

            Paddr sdtPaddr = 0;
            sl::Span<char> sdtPtrBuff { reinterpret_cast<char*>(&sdtPaddr), ptrSize };
            CopyFromPhysical(ptrPaddr, sdtPtrBuff);

            acpiTables[i].paddr = sdtPaddr;

            sl::Sdt sdt;
            sl::Span<char> sdtBuff { reinterpret_cast<char*>(&sdt), sizeof(sdt) };
            NPK_CHECK(CopyFromPhysical(sdtPaddr, sdtBuff) == sizeof(sl::Sdt), );
            sl::MemCopy(acpiTables[i].signature, sdt.signature, 4);

            const size_t offset = sdtPaddr & PageMask();
            const uintptr_t sdtVaddr = virtBase + offset;
            acpiTables[i].vaddr = reinterpret_cast<void*>(sdtVaddr);

            const Paddr sdtTop = sdtPaddr + sdt.length;
            for (size_t m = AlignDownPage(sdtPaddr); m < sdtTop; 
                m += PageSize(), virtBase += PageSize())
            {
                const auto status = SetKernelMap(virtBase, m, {});
                NPK_CHECK(status == VmStatus::Success, );
            }

            const auto conv = sl::ConvertUnits(sdt.length);
            Log("Mapped acpi table: %.4s v%u, %p -> 0x%tx, %zu.%zu %sB", 
                LogLevel::Info, sdt.signature, sdt.revision, acpiTables[i].vaddr,
                acpiTables[i].paddr, conv.major, conv.minor, conv.prefix);
        }
    }

    sl::Opt<sl::Sdt*> GetAcpiTable(sl::StringSpan signature)
    {
        for (size_t i = 0; i < acpiTables.Size(); i++)
        {
            if (sl::MemCompare(acpiTables[i].signature, signature.Begin(), 4) != 0)
                continue;

            return static_cast<sl::Sdt*>(acpiTables[i].vaddr);
        }

        return {};
    }
}
