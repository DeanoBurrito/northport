#include <config/AcpiTables.h>
#include <arch/Platform.h>
#include <debug/Log.h>
#include <NativePtr.h>
#include <Memory.h>

namespace Npk::Config
{
    Rsdp* rsdp = nullptr;
    sl::NativePtr sdtPointers;
    size_t sdtCount;
    Sdt* (*GetSdt)(size_t i);

    Sdt* GetSdt32(size_t i)
    {
        return AddHhdm(reinterpret_cast<Sdt*>(sdtPointers.As<uint32_t>()[i]));
    }

    Sdt* GetSdt64(size_t i)
    {
        //The SDT header used for acpi tables is 36 bytes long, meaning the xsdt addresses will always be mislaigned by 4 bytes (uint32_t).
        //While we dont need to do this, a misaligned read is still technically UB, and I'd like to avoid that.
        uint32_t low = sdtPointers.As<uint32_t>()[i * 2];
        uint32_t high = sdtPointers.As<uint32_t>()[i * 2 + 1];
        return AddHhdm(reinterpret_cast<Sdt*>(low | (uint64_t)high));
    }

    void SetRsdp(void* providedRsdp)
    {
        ASSERT(providedRsdp != nullptr, "RSDP was null.");
        
        rsdp = static_cast<Rsdp*>(providedRsdp);
        if ((uintptr_t)rsdp < hhdmBase)
            rsdp = AddHhdm(rsdp);
        
        if (rsdp->revision > 1)
        {
            Xsdt* xsdt = AddHhdm(sl::NativePtr(rsdp->xsdt).As<Xsdt>());
            sdtPointers = xsdt->entries;
            sdtCount = (xsdt->length - sizeof(Sdt)) / 8;
            GetSdt = GetSdt64;
        }
        else
        {
            Rsdt* rsdt = AddHhdm(sl::NativePtr(rsdp->rsdt).As<Rsdt>());
            sdtPointers = rsdt->entries;
            sdtCount = (rsdt->length - sizeof(Sdt)) / 4;
            GetSdt = GetSdt32;
        }
        Log("RSDP: revision=%u, rsdt=0x%x, xsdt=0x%lx, pointers=0x%lx", LogLevel::Info, rsdp->revision, rsdp->rsdt, rsdp->xsdt, sdtPointers.raw);
        
        for (size_t i = 0; i < sdtCount; i++)
        {
            LogAcpiTable(GetSdt(i));
            if (VerifyChecksum(GetSdt(i)))
                Log("Acpi table %.4s failed checksum verification.", LogLevel::Error, GetSdt(i)->signature);
        }
    }

    bool VerifyChecksum(const Sdt* table)
    {
        size_t checksum = 0;
        const uint8_t* accessor = reinterpret_cast<const uint8_t*>(table);
        for (size_t i = 0; i < table->length; i++)
            checksum += accessor[i];
        
        return (checksum & 0xFF) != 0;
    }

    sl::Opt<Sdt*> FindAcpiTable(const char* signature)
    {
        for (size_t i = 0; i < sdtCount; i++)
        {
            Sdt* test = GetSdt(i);
            if (sl::memcmp(signature, test->signature, SdtSigLength) == 0)
                return test;
        }

        return {};
    }

    void LogAcpiTable(const Sdt* table)
    {
        char sig[5];
        sl::memcopy(table->signature, sig, 4);
        sig[4] = 0;

        char oem[7];
        sl::memset(oem, 0, 7);
        const size_t oemLen = sl::memfirst(table->oem, ' ', 6);
        sl::memcopy(table->oem, oem, oemLen != (size_t)-1 ? oemLen : 6);
        
        Log("Acpi table: signature=%s, oem=%s, addr=0x%lx, len=0x%x", LogLevel::Verbose, 
            sig, oem, (uintptr_t)table - hhdmBase, table->length);
    }
}
