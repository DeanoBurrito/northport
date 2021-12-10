#pragma once

#include <acpi/Defs.h>
#include <stddef.h>
#include <NativePtr.h>

namespace Kernel::ACPI
{
    class AcpiTables
    {
    private:
        size_t revision;
        uint64_t (*GetAddrAtIndex)(sl::NativePtr rootTable, size_t index);
        sl::NativePtr rootTable;
        size_t sdtCount;

    public:
        static AcpiTables* Global();
        static bool ChecksumValid(SdtHeader* header);
        
        void Init(uint64_t rsdptr);
        SdtHeader* Find(SdtSignature which) const;

        void PrintSdt(SdtHeader* header) const;
    };
}
