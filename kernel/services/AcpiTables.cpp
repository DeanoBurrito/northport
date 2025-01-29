#include <services/AcpiTables.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <Entry.h>
#include <containers/List.h>
#include <NativePtr.h>
#include <Memory.h>

namespace Npk::Services
{
    constexpr size_t TentativeSdtCount = 32;
    constexpr size_t TentativeSdtLength = 0x200;

    struct AcpiTableView
    {
        sl::FwdListHook hook;
        const Sdt* table;
    };

    uintptr_t rawRsdp = 0;
    sl::FwdList<AcpiTableView, &AcpiTableView::hook> tables;

    void SetRsdp(uintptr_t rsdp)
    {
        VALIDATE_(rsdp != 0, );
        rawRsdp = rsdp;

        auto rsdpView = static_cast<const Rsdp*>(EarlyVmAlloc(rsdp, sizeof(Rsdp), false, false, "rsdp"));
        VALIDATE_(rsdpView != nullptr, );
        Log("RSDP set: 0x%tx, revision=%u", LogLevel::Info, rsdp, rsdpView->revision);

        if (rsdpView->revision > 1)
        {
            auto xsdtView = static_cast<const Xsdt*>(EarlyVmAlloc(rsdpView->xsdt, sizeof(Xsdt) + 
                TentativeSdtCount * 8, false, false, "xsdt"));
            VALIDATE_(xsdtView != nullptr, );

            const size_t sdtCount = (xsdtView->length - sizeof(Sdt)) / 8;
            if (sdtCount > TentativeSdtCount)
            {
                xsdtView = static_cast<const Xsdt*>(EarlyVmAlloc(rsdpView->xsdt, 
                    sizeof(Xsdt) + sdtCount * 8, false, false, "xsdt+"));
            }

            auto xsdtViewStore = NewWired<AcpiTableView>();
            VALIDATE_(xsdtViewStore != nullptr, );
            xsdtViewStore->table = xsdtView;
            tables.PushBack(xsdtViewStore);

            sl::CNativePtr ptrs = xsdtView + 1;
            for (size_t i = 0; i < sdtCount; i++)
            {
                //these 64bit addresses are misaligned by 4-bytes, resulting in UB. :(
                const uintptr_t addr = ptrs.As<uint32_t>()[i * 2] | (uint64_t)ptrs.As<uint32_t>()[i * 2 + 1];
                auto sdt = static_cast<const Sdt*>(EarlyVmAlloc(addr, TentativeSdtLength, false, false, "sdt scan"));
                VALIDATE_(sdt != nullptr, );

                auto sdtStore = NewWired<AcpiTableView>();
                VALIDATE_(sdt != nullptr, );
                if (sdt->length > TentativeSdtLength)
                    sdt = static_cast<const Sdt*>(EarlyVmAlloc(addr, sdt->length, false, false, "sdt scan+"));

                sdtStore->table = sdt;
                tables.PushBack(sdtStore);
            }
        }
        else
        {
            auto rsdtView = static_cast<const Rsdt*>(EarlyVmAlloc(rsdpView->rsdt, sizeof(Rsdt) +
                TentativeSdtCount * 4, false, false, "rsdt"));
            VALIDATE_(rsdtView != nullptr, );

            const size_t sdtCount = (rsdtView->length - sizeof(Sdt)) / 4;
            if (sdtCount > TentativeSdtCount)
            {
                rsdtView = static_cast<const Rsdt*>(EarlyVmAlloc(rsdpView->rsdt,
                        sizeof(Rsdt) + sdtCount * 8, false, false, "rsdt+"));
            }

            auto rsdtViewStore = NewWired<AcpiTableView>();
            VALIDATE_(rsdtViewStore != nullptr, );
            rsdtViewStore->table = rsdtView;

            sl::CNativePtr ptrs = rsdtView + 1;
            for (size_t i = 0; i < sdtCount; i++)
            {
                const uintptr_t addr = ptrs.As<uint32_t>()[i];
                auto sdt = static_cast<const Sdt*>(EarlyVmAlloc(addr, TentativeSdtLength, false, false, "sdt scan"));
                VALIDATE_(sdt != nullptr, );

                auto sdtStore = NewWired<AcpiTableView>();
                VALIDATE_(sdt != nullptr, );
                if (sdt->length > TentativeSdtLength)
                    sdt = static_cast<const Sdt*>(EarlyVmAlloc(addr, sdt->length, false, false, "sdt scan+"));

                sdtStore->table = sdt;
                tables.PushBack(sdtStore);
            }
        }

        for (auto it = tables.Begin(); it != tables.End(); ++it)
        {
            char oem[sizeof(Sdt::oem) + 1];
            char oemTable[sizeof(Sdt::oemTable) + 1];
            sl::MemSet(oem, 0, sizeof(oem));
            sl::MemSet(oemTable, 0, sizeof(oemTable));

            sl::MemCopy(oem, it->table->oem,
                sl::MemFind(it->table->oem, ' ', sizeof(Sdt::oem)));
            sl::MemCopy(oemTable, it->table->oemTable,
                sl::MemFind(it->table->oemTable, ' ', sizeof(Sdt::oemTable)));

            Log("ACPI table: %.4s, rev=%u, length=0x%" PRIu32 ", oem=%s %s", LogLevel::Info,
                it->table->signature, it->table->revision, it->table->length, oem, oemTable);
        }
    }

    sl::Opt<uintptr_t> GetRsdp()
    {
        if (rawRsdp == 0)
            return {};
        return rawRsdp;
    }

    const Sdt* FindAcpiTable(sl::StringSpan signature)
    {
        for (auto it = tables.Begin(); it != tables.End(); ++it)
        {
            if (!sl::MemCompare(it->table->signature, signature.Begin(), 4))
                return it->table;
        }

        return nullptr;
    }

    const RhctNode* FindRhctNode(const Rhct* rhct, RhctNodeType type, const RhctNode* begin)
    {
        VALIDATE_(rhct != nullptr, nullptr);

        sl::CNativePtr scan = rhct;
        const uintptr_t rhctEnd = scan.raw + rhct->length;
        scan = scan.Offset(rhct->nodesOffset);

        while (scan.raw < rhctEnd)
        {
            auto node = scan.As<const RhctNode>();
            if (node->type == type && node > begin)
                return node;

            scan.raw += node->length;
        }

        return nullptr;
    }
}
