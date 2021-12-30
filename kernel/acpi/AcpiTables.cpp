#include <acpi/AcpiTables.h>
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

            XSDT* xsdt = rootTable.As<XSDT>();
            sdtCount = (xsdt->length - sizeof(SdtHeader)) / 8;
        }
        else
        {
            Log("System ACPI tables are v0 format (32 bit addresses).", LogSeverity::Warning);

            GetAddrAtIndex = Get32BitTableAddr;
            rootTable = rsdp1->rsdtAddress;

            RSDT* rsdt = rootTable.As<RSDT>();
            sdtCount = (rsdt->length - sizeof(SdtHeader)) / 4;
        }

        Logf("%u acpi tables detected.", LogSeverity::Verbose, sdtCount);

        //dump all the acpi tables
        for (size_t i = 0; i < sdtCount; i++)
        {
            SdtHeader* currentTable = reinterpret_cast<SdtHeader*>(GetAddrAtIndex(rootTable, i));
            PrintSdt(currentTable);
        }
    }

    SdtHeader* AcpiTables::Find(SdtSignature sig) const
    {
        for (size_t i = 0; i < sdtCount; i++)
        {
            SdtHeader* currentTable = reinterpret_cast<SdtHeader*>(GetAddrAtIndex(rootTable, i));
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
        char* sig = new char[5];
        sl::memcopy(header->signature, sig, 4);
        sig[4] = 0;

        char* oemId = new char[7];
        sl::memcopy(header->oemId, oemId, 6);
        oemId[7] = 0;

        Logf("SDT Header: sig=%s, len=0x%x, rev=%u, oemId=%s, addr=0x%lx", LogSeverity::Verbose, sig, header->length, header->revision, oemId, (uint64_t)header);
        if (!ChecksumValid(header))
            Log("  \\- Table has invalid checksum, data may not be valid.", LogSeverity::Error);

        delete[] sig;
        delete[] oemId;
    }
}
