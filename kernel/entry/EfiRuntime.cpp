#include <private/Entry.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <lib/Efi.hpp>

//TODO: there's a lot of fatal failure paths in this file, be graceful.
namespace Npk
{
    sl::EfiRuntimeServices* efiRtTable = nullptr;

    NpkStatus TryEnableEfiRuntimeServices(const Loader::EfiDetails& details, 
        uintptr_t& virtBase)
    {
        constexpr size_t MaxDescSize = 256;
        constexpr size_t MaxPreservedMaps = 16;

        char descBuffer[MaxDescSize];
        auto ReadDesc = [&](size_t i) -> auto
        {
            const Paddr addr = details.memmapBase + i * details.memmapDescSize;
            CopyFromPhysical(addr, { descBuffer, details.memmapDescSize });

            return reinterpret_cast<const sl::EfiMemoryDescriptor*>(descBuffer);
        };

        //optimization: the firmware releases control of the following
        //memory descriptor types after exiting boot services, so it wont
        //need them during SetVirtualAddressMap(). On systems with large
        //amounts of usable ram (type: conventional) this can speed things
        //up quite a bit.
        auto DescNeedsIdentityMap = [](const sl::EfiMemoryDescriptor* desc) -> bool
        {
            if (desc->attributes.Has(sl::EfiMemoryFlag::Runtime))
                return true;

           switch (desc->type)
            {
            case sl::EfiMemoryType::Conventional: 
                [[fallthrough]];
            case sl::EfiMemoryType::LoaderCode: 
                [[fallthrough]];
            case sl::EfiMemoryType::LoaderData: 
                [[fallthrough]];
            case sl::EfiMemoryType::BootServicesCode: 
                [[fallthrough]];
            case sl::EfiMemoryType::BootServicesData:
                return false;

            default:
                return true;
            }
        };

        if (!ReadConfigUint("npk.allow_efi_runtime", true))
        {
            Log("EFI runtime services unavailable: disabled via command line",
                LogLevel::Error);

            return NpkStatus::BadConfig;
        }

        if (details.memmapSize == 0 || details.memmapDescSize > MaxDescSize 
            || details.memmapDescSize < sizeof(sl::EfiMemoryDescriptor)
            || details.memmapDescSize % alignof(sl::EfiMemoryDescriptor) != 0)
        {
            Log("EFI runtime services unavailable: bad memory map format",
                LogLevel::Error);

            return NpkStatus::InvalidArg;
        }

        if (details.memmapSize % details.memmapDescSize != 0)
        {
            Log("EFI memory map size (0x%zx) not multiple of descriptor size"
                " (0x%zx)", LogLevel::Warning, details.memmapSize, 
                details.memmapDescSize);
        }

        sl::EfiSystemTable sysTable {};
        if (CopyFromPhysical(details.systemTable, 
            { (char*)&sysTable, sizeof(sysTable) }) != sizeof(sysTable))
        {
            Log("EFI runtime services unavailable: failed to read system table",
                LogLevel::Error);

            return NpkStatus::InternalError;
        }

        if (sysTable.hdr.signature != sl::EfiSignatureSystemTable)
        {
            Log("EFI runtime unavailable: bad SystemTable signature 0x%" PRIx64,
                LogLevel::Error, sysTable.hdr.signature);

            return NpkStatus::InternalError;
        }

        const Paddr rtPaddr = reinterpret_cast<Paddr>(sysTable.runtimeServices);
        const size_t descCount = details.memmapSize / details.memmapDescSize;

        //ensure runtime table is within a memory region flagged for runtime
        //use.
        bool rtTableFound = false;
        size_t rtDescCount = 0;
        for (size_t i = 0; i < descCount; i++)
        {
            auto* desc = ReadDesc(i);

            if (!desc->attributes.Has(sl::EfiMemoryFlag::Runtime))
                continue;
            rtDescCount++;

            if (!rtTableFound)
            {
                const size_t bytes = desc->numberOfPages * PageSize();

                if (rtPaddr >= desc->physicalStart 
                    && rtPaddr < desc->physicalStart + bytes)
                    rtTableFound = true;
            }
        }

        if (rtTableFound)
        {
            Log("EFI RT table valid, %zu (of %zu) memmap descriptors needed.",
                LogLevel::Verbose, rtDescCount, descCount);
        }
        else
        {
            Log("EFI runtime services unavailable: RT table not found in any"
                " runtime region", LogLevel::Error);

            return NpkStatus::InternalError;
        }

        const size_t rtDescsLen = rtDescCount * details.memmapDescSize;
        const uintptr_t rtDescsBase = virtBase;
        for (size_t off = 0; off < rtDescsLen; 
            off += PageSize(), virtBase += PageSize())
        {
            auto* page = AllocPage(false);
            auto paddr = LookupPagePaddr(page);

            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success);
        }
        sl::MemSet(reinterpret_cast<void*>(rtDescsBase), 0, rtDescsLen);

        Log("EFI runtime descriptors at 0x%tx, %zu bytes", LogLevel::Verbose,
            rtDescsBase, rtDescsLen);

        //populate our efi memory map with the runtime entries from the firmware
        //provided one. Also identity map it and take note of any existing
        //lower half mappings.
        //If you're wondering why the kernel would have any lower half mapppings
        //the answer is for platform specific goop like AP bringup or sometimes
        //interacting with firmware. This code temporarily removes these maps
        //to ensure everything is identity mapped, but they are restored later.
        uintptr_t preserved[MaxPreservedMaps];
        size_t preservedCount = 0;

        sl::EfiVirtualAddress rtVaddr = 0;
        size_t rtDescIndex = 0;
        const VmFlags rtFlags = VmFlag::Write | VmFlag::Fetch;

        for (size_t i = 0; i < descCount; i++)
        {
            auto* desc = ReadDesc(i);
            if (desc->numberOfPages == 0)
                continue;

            const size_t bytes = desc->numberOfPages * PageSize();
            const bool isRuntime =
                desc->attributes.Has(sl::EfiMemoryFlag::Runtime);

            if (!DescNeedsIdentityMap(desc))
                continue;

            const uintptr_t highBase = virtBase;

            for (size_t off = 0; off < bytes; off += PageSize())
            {
                const uintptr_t paddr = desc->physicalStart + off;
                auto result = SetKernelMap(paddr, paddr, rtFlags);

                if (result == NpkStatus::AlreadyMapped)
                {
                    //something is already mapped here, backup the address
                    //and then re-map it as we expect it.
                    Paddr prevPaddr = 0;
                    NPK_ASSERT(ClearKernelMap(paddr, &prevPaddr)
                        == NpkStatus::Success);
                    NPK_ASSERT(SetKernelMap(paddr, paddr, rtFlags)
                        == NpkStatus::Success);
                    NPK_ASSERT(prevPaddr == paddr);

                    NPK_ASSERT(preservedCount < MaxPreservedMaps);
                    preserved[preservedCount++] = paddr;
                }
                else
                    NPK_ASSERT(result == NpkStatus::Success);

                if (isRuntime)
                {
                    NPK_ASSERT(SetKernelMap(virtBase, paddr, rtFlags)
                        == NpkStatus::Success);
                    virtBase += PageSize();
                }
            }

            if (!isRuntime)
                continue;

            if (rtPaddr >= desc->physicalStart
                && rtPaddr < desc->physicalStart + bytes)
                rtVaddr = highBase + (rtPaddr - desc->physicalStart);

            auto* store = reinterpret_cast<sl::EfiMemoryDescriptor*>(
                rtDescsBase + rtDescIndex * details.memmapDescSize);
            sl::MemCopy(store, desc, details.memmapDescSize);
            store->virtualStart = highBase;
            
            rtDescIndex++;
        }

        HwFlushTlbAll();

        NpkStatus result = NpkStatus::Success;
        auto* rtIdent = reinterpret_cast<sl::EfiRuntimeServices*>(rtPaddr);
        if (rtIdent->hdr.signature != sl::EfiSignatureRtsTable)
        {
            Log("EFI runtime unavailable: bad RT table signature 0x%" PRIx64,
                LogLevel::Error, rtIdent->hdr.signature);
            result = NpkStatus::InternalError;
        }
        else
        {
            auto efiResult = rtIdent->SetVirtualAddressMap(
                static_cast<sl::EfiUintN>(rtDescsLen),
                static_cast<sl::EfiUintN>(details.memmapDescSize),
                details.memmapDescVersion,
                reinterpret_cast<sl::EfiMemoryDescriptor*>(rtDescsBase)
                );

            if (efiResult != 0)
            {
                Log("EFI runtime services unavailable: SetVirtualAddressMap() "
                    "returned %i", LogLevel::Error, efiResult);

                result = NpkStatus::NotAvailable;
            }
        }

        //remove identity map, taking care to preserve any pre-existing
        //mappings.
        for (size_t i = 0; i < descCount; i++)
        {
            auto* desc = ReadDesc(i);
            if (desc->numberOfPages == 0)
                continue;

            if (!DescNeedsIdentityMap(desc))
                continue;

            const size_t bytes = desc->numberOfPages * PageSize();

            for (size_t off = 0; off < bytes; off += PageSize())
            {
                const uintptr_t paddr = desc->physicalStart + off;
                bool skip = false;

                for (size_t k = 0; k < preservedCount && !skip; k++)
                    skip = preserved[k] == paddr;
                if (!skip)
                    ClearKernelMap(paddr, nullptr);
            }
        }
        HwFlushTlbAll();

        if (result != NpkStatus::Success)
        {
            for (size_t i = rtDescsBase; i < virtBase; i += PageSize())
            {
                Paddr paddr;
                NPK_ASSERT(ClearKernelMap(i, &paddr) == NpkStatus::Success);

                if (i < rtDescsBase + rtDescsLen)
                {
                    auto page = LookupPageInfo(paddr);
                    FreePage(page);
                }
            }

            //we undid all higher half mappings so it's safe to reset
            //`virtBase` back to how it was, to avoid leaking address space.
            virtBase = rtDescsBase;
            HwFlushTlbAll();

            return result;
        }

        efiRtTable = reinterpret_cast<sl::EfiRuntimeServices*>(rtVaddr);
        Log("EFI runtime services available, table at %p", LogLevel::Info,
            efiRtTable);

        return NpkStatus::Success;
    }

    sl::Opt<sl::EfiRuntimeServices*> GetEfiRtServices()
    {
        if (efiRtTable == nullptr)
            return {};
        return efiRtTable;
    }
}
