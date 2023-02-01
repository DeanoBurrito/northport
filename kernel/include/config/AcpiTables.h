#pragma once

#include <Optional.h>
#include <config/AcpiDefs.h>

namespace Npk::Config
{
    void SetRsdp(void* rsdp);

    bool VerifyChecksum(const Sdt* table);
    sl::Opt<const Sdt*> FindAcpiTable(const char* signature);
    void PrintSdt(const Sdt* table);
}
