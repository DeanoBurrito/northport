#pragma once

#include <stdint.h>
#include <Optional.h>
#include <Span.h>
#include <services/AcpiSpecDefs.h>

namespace Npk::Services
{
    void SetRsdp(uintptr_t rsdp);
    sl::Opt<uintptr_t> GetRsdp();

    const Sdt* FindAcpiTable(sl::StringSpan signature);
    const RhctNode* FindRhctNode(const Rhct* rhct, RhctNodeType type, const RhctNode* begin = nullptr);
}
