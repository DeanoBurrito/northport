#include <core/Acpi.h>
#include <core/PmAccess.h>
#include <core/Log.h>
#include <Memory.h>

namespace Npk::Core
{
    constexpr size_t MaxSdtCount = 32;

    sl::Opt<Paddr> rsdp = {};
    Paddr acpiSdts[MaxSdtCount];
    size_t acpiSdtCount = 0;

    sl::Opt<Paddr> Rsdp(sl::Opt<Paddr> set)
    {
        if (!set.HasValue())
            return rsdp;

        auto prev = rsdp;
        rsdp = *set;

        char workingBuffer[0x500];
        CopyFromPm(*rsdp, workingBuffer);
        auto rsdptr = reinterpret_cast<const struct Rsdp*>(workingBuffer);

        Log("RSDP set: 0x%tx, oem=%.6s, rev=%u, rsdt=0x%" PRIx32", xsdt=0x%" PRIx64,
            LogLevel::Info, *set, rsdptr->oem, rsdptr->revision, rsdptr->rsdt, 
            rsdptr->xsdt);

        if (rsdptr->revision > 1 && rsdptr->xsdt != 0)
        {
            CopyFromPm(rsdptr->xsdt, workingBuffer);
            auto xsdt = reinterpret_cast<const Xsdt*>(workingBuffer);
            
            acpiSdtCount = (xsdt->length - sizeof(Sdt)) / 8;
            ASSERT_(acpiSdtCount <= MaxSdtCount);

            for (size_t i = 0; i < acpiSdtCount; i++) //64-bit values are misaligned here
                sl::MemCopy(&acpiSdts[i], &xsdt->entries[i], 8);
        }
        else
        {
            CopyFromPm(rsdptr->rsdt, workingBuffer);
            auto rsdt = reinterpret_cast<const Rsdt*>(workingBuffer);

            acpiSdtCount = (rsdt->length - sizeof(Sdt)) / 4;
            ASSERT_(acpiSdtCount <= MaxSdtCount);

            for (size_t i = 0; i < acpiSdtCount; i++)
                acpiSdts[i] = rsdt->entries[i];
        }
        rsdptr = nullptr;

        return prev;
    }

    size_t CopyAcpiTable(sl::StringSpan signature,  sl::Span<char> buffer)
    {
        if (!rsdp.HasValue())
            return 0;
        if (signature.Size() < 4)
            return 0;

        Sdt sdt {};
        sl::Span<char> buff(reinterpret_cast<char*>(&sdt), sizeof(Sdt));

        for (size_t i = 0; i < acpiSdtCount; i++)
        {
            if (CopyFromPm(acpiSdts[i], buff) != sizeof(Sdt))
                return 0;
            if (sl::MemCompare(&sdt.signature, signature.Begin(), 4) != 0)
                continue;
            if (sdt.length > buffer.Size())
                return sdt.length;

            return CopyFromPm(acpiSdts[i], buffer.Subspan(0, sdt.length));
        }

        return 0;
    }
}
