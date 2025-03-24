#pragma once

#include <Optional.h>
#include <Span.h>
#include <core/AcpiSpecDefs.h>

namespace Npk::Core
{
    sl::Opt<Paddr> Rsdp(sl::Opt<Paddr> set = {});

    //returns the number of bytes required to store the data, or 0 if not found.
    //If the table contents can fit into the buffer, they are copied in, otherwise
    //buffer contents are untouched.
    size_t CopyAcpiTable(sl::StringSpan signature,  sl::Span<char> buffer);

    SL_ALWAYS_INLINE
    bool AcpiTableExists(sl::StringSpan signature)
    {
        return CopyAcpiTable(signature, {}) != 0;
    }
}
