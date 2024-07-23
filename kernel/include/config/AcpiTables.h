#pragma once

#include <Optional.h>
#include <config/AcpiDefs.h>

namespace Npk::Config
{
    void SetRsdp(uintptr_t rsdp);
    sl::Opt<uintptr_t> GetRsdp();

    bool VerifyChecksum(const Sdt* table);
    sl::Opt<const Sdt*> FindAcpiTable(const char* signature);
    void PrintSdt(const Sdt* table);

    sl::Opt<const RhctNode*> FindRhctNode(const Rhct* rhct, RhctNodeType type, const RhctNode* begin = nullptr);
}
