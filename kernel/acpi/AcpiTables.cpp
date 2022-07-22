#include <acpi/AcpiTables.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::ACPI
{
    uint64_t Get64BitTableAddr(sl::NativePtr rootTable, size_t index)
    {
        return rootTable.As<XSDT>()->entryAddresses[index];
    }

    uint64_t Get32BitTableAddr(sl::NativePtr rootTable, size_t index)
    {
        return static_cast<uint64_t>(rootTable.As<RSDT>()->entryAddresses[index]);
    }

    AcpiTables acpiTablesInstance;
    AcpiTables* AcpiTables::Global()
    { return &acpiTablesInstance; }

    bool AcpiTables::ChecksumValid(SdtHeader* header)
    {
        uint64_t checksum = 0;
        sl::NativePtr ptr(header);

        for (size_t i = 0; i < header->length; i++)
            checksum += *ptr.As<uint8_t>(i);

        return (checksum & 0xFF) == 0;
    }

    size_t AcpiTables::GetRevision() const
    { return revision; }

    void AcpiTables::Init(uint64_t rsdptr)
    {
        RSDP* rsdp1 = sl::NativePtr(rsdptr).As<RSDP>();
        revision = rsdp1->revision;
        if (rsdp1->revision == 2)
        {
            Log("System ACPI tables are v2 format (64 bit addresses).", LogSeverity::Verbose);
            
            GetAddrAtIndex = Get64BitTableAddr;
            rootTable = reinterpret_cast<RSDP2*>(rsdp1)->xsdtAddress;
            rootTable.ptr = EnsureHigherHalfAddr(rootTable.ptr);

            XSDT* xsdt = rootTable.As<XSDT>();
            sdtCount = (xsdt->length - sizeof(SdtHeader)) / 8;
        }
        else
        {
            Log("System ACPI tables are v0 format (32 bit addresses).", LogSeverity::Warning);

            GetAddrAtIndex = Get32BitTableAddr;
            rootTable = rsdp1->rsdtAddress;
            rootTable.ptr = EnsureHigherHalfAddr(rootTable.ptr);

            RSDT* rsdt = rootTable.As<RSDT>();
            sdtCount = (rsdt->length - sizeof(SdtHeader)) / 4;
        }

        Logf("%u ACPI tables detected.", LogSeverity::Verbose, sdtCount);

        //dump all the acpi tables
        for (size_t i = 0; i < sdtCount; i++)
        {
            SdtHeader* currentTable = EnsureHigherHalfAddr(reinterpret_cast<SdtHeader*>(GetAddrAtIndex(rootTable, i)));
            PrintSdt(currentTable);
        }
    }

    SdtHeader* AcpiTables::Find(SdtSignature sig) const
    {
        for (size_t i = 0; i < sdtCount; i++)
        {
            SdtHeader* currentTable = EnsureHigherHalfAddr(reinterpret_cast<SdtHeader*>(GetAddrAtIndex(rootTable, i)));
            if (sl::memcmp(currentTable->signature, SdtSignatureLiterals[(size_t)sig], 4) == 0)
            {
                if (!ChecksumValid(currentTable))
                    Logf("ACPI table %s found, but has a bad checksum.", LogSeverity::Error, SdtSignatureLiterals[(size_t)sig]);
                
                return currentTable;
            }
        }

        Logf("Could not find ACPI table: %s", LogSeverity::Error, SdtSignatureLiterals[(size_t)sig]);
        return nullptr;
    }

    void AcpiTables::PrintSdt(SdtHeader* header) const
    {
        char signature[5] = { 0, 0, 0, 0, 0 };
        sl::memcopy(header->signature, signature, 4);
        char oem[7] = { 0, 0, 0, 0, 0, 0, 0};
        sl::memcopy(header->oemId, oem, sl::min(6ul, sl::memfirst(header->oemId, ' ', 0)));

        Logf("Found ACPI SDT: signature=%s, oem=%s, rev=%u, addr=0x%lx", LogSeverity::Verbose, 
            signature, oem, header->revision, (size_t)header);
        if (!ChecksumValid(header))
            Logf("ACPI table has bad checksum: %s", LogSeverity::Warning, signature);
    }
}
