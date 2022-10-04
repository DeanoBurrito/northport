#pragma once

#include <Optional.h>
#include <acpi/Defs.h>

namespace Npk::Acpi
{
    void SetRsdp(void* rsdp);

    bool VerifyChecksum(const Sdt* table);
    sl::Opt<Sdt*> FindTable(const char* signature);
    void LogTable(const Sdt* table);
}
