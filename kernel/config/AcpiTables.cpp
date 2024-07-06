#include <config/AcpiTables.h>
#include <debug/Log.h>
#include <memory/VmObject.h>
#include <Memory.h>
#include <containers/LinkedList.h>
#include <NativePtr.h>

namespace Npk::Config
{
    sl::LinkedList<VmObject> tables;

    void SetRsdp(void* providedRsdp)
    {
        ASSERT(providedRsdp != nullptr, "RSDP is null.");
        
        VmObject rsdpWindow(sizeof(Rsdp), (uintptr_t)providedRsdp, VmFlag::Mmio);
        const Rsdp& rsdpAccess = *rsdpWindow->As<Rsdp>();

        if (rsdpAccess.revision > 1)
        {
            //version 2.0+, use xsdt
            VmObject xsdtWindow(sizeof(Xsdt), (uintptr_t)rsdpAccess.xsdt, VmFlag::Mmio);
            VmObject fullXsdt(xsdtWindow->As<Xsdt>()->length, (uintptr_t)rsdpAccess.xsdt, VmFlag::Mmio);

            const size_t tableCount = (fullXsdt->As<Xsdt>()->length - sizeof(Sdt)) / 8;

            sl::NativePtr ptrs = fullXsdt->Offset(sizeof(Sdt));
            for (size_t i = 0; i < tableCount; i++)
            {
                //these 64bit addresses are misaligned by 4-bytes, resulting in UB. :(
                const uintptr_t addr = ptrs.As<uint32_t>()[i * 2] | (uint64_t)ptrs.As<uint32_t>()[i * 2 + 1];
                VmObject tempWindow(sizeof(Sdt), addr, VmFlag::Mmio);

                const size_t length = tempWindow->As<Sdt>()->length;
                tables.EmplaceBack(length, addr, VmFlag::Mmio);
            }

            tables.EmplaceBack(fullXsdt.Size(), (uintptr_t)rsdpAccess.xsdt, VmFlag::Mmio);
        }
        else
        {
            //version 1 (stored as 0 sometimes), use rsdt
            VmObject rsdtWindow(sizeof(Rsdt), (uintptr_t)rsdpAccess.rsdt, VmFlag::Mmio);
            VmObject fullRsdt(rsdtWindow->As<Rsdt>()->length, (uintptr_t)rsdpAccess.rsdt, VmFlag::Mmio);

            const size_t tableCount = (fullRsdt->As<Rsdt>()->length - sizeof(Sdt)) / 4;

            sl::NativePtr ptrs = fullRsdt->Offset(sizeof(Sdt));
            for (size_t i = 0; i < tableCount; i++)
            {
                const uintptr_t addr = ptrs.As<uint32_t>()[i];
                VmObject tempWindow(sizeof(Sdt), addr, VmFlag::Mmio);
                
                const size_t length = tempWindow->As<Sdt>()->length;
                tables.EmplaceBack(length, addr, VmFlag::Mmio);
            }

            tables.EmplaceBack(fullRsdt.Size(), (uintptr_t)rsdpAccess.rsdt, VmFlag::Mmio);
        }

        Log("Rsdp set: %p, revision=%u, tables=%zu", LogLevel::Info, 
            providedRsdp, rsdpAccess.revision, tables.Size());

        for (auto it = tables.Begin(); it != tables.End(); ++it)
            PrintSdt(it->Ptr().As<const Sdt>());
    }

    bool VerifyChecksum(const Sdt* table)
    {
        size_t checksum = 0;
        const uint8_t* accessor = reinterpret_cast<const uint8_t*>(table);
        for (size_t i = 0; i < table->length; i++)
            checksum += accessor[i];
        
        return (checksum & 0xFF) != 0;
    }

    sl::Opt<const Sdt*> FindAcpiTable(const char* signature)
    {
        for (auto it = tables.Begin(); it != tables.End(); ++it)
        {
            const Sdt* sdt = it->Ptr().As<const Sdt>();
            if (sl::memcmp(sdt->signature, signature, SdtSigLength) == 0)
                return sdt;
        }

        return {};
    }

    void PrintSdt(const Sdt* table)
    {
        char oem[7];
        const size_t oemLength = sl::memfirst(table->oem, ' ', 6);
        sl::memset(oem, 0, 7);
        sl::memcopy(table->oem, oem, oemLength);

        Log("Acpi Sdt: sig=%.4s, oem=%s, revision=%u, length=0x%" PRIu32, LogLevel::Verbose,
            table->signature, oem, table->revision, table->length);
    }

    sl::Opt<const RhctNode*> FindRhctNode(const Rhct* rhct, RhctNodeType type, const RhctNode* begin)
    {
        ASSERT(rhct != nullptr, "RHCT is null");

        sl::CNativePtr scan = rhct;
        const uintptr_t rhctEnd = scan.raw + rhct->length;
        scan = scan.Offset(rhct->nodesOffset);

        while (scan.raw < rhctEnd)
        {
            const RhctNode* node = scan.As<const RhctNode>();
            if (node->type == type && node > begin)
                return node;

            scan.raw += node->length;
        }

        return {};
    }
}
