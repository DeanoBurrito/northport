#include <EntryPrivate.hpp>
#include <Core.hpp>

namespace Npk
{
    sl::EfiRuntimeServices* efiRtTable = nullptr;

    void TryEnableEfiRtServices(Paddr systemTable, uintptr_t& virtBase)
    {
        Log("Efi runtime services init not implemented", LogLevel::Error);
        (void)systemTable;
        (void)virtBase;

        /* TODO: ok, so this is going to suck a bit. My understand is the following:
         * - we need to call efiRtTable->SetVirtualAddressMap()
         * - this call needs to be made with the original efi mappings in-place
         * - meaning we need to recreate the efi mappings, switch to then, then switch back
         * - we then need to free all the temporary mappings we made earlier
         */
    }

    sl::Opt<sl::EfiRuntimeServices*> GetEfiRtServices()
    {
        if (efiRtTable == nullptr)
            return {};
        return efiRtTable;
    }
}
