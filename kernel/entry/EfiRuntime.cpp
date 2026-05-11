#include <private/Entry.hpp>
#include <Core.hpp>
#include <Vm.hpp>
#include <lib/Efi.hpp>

namespace Npk
{
    sl::EfiRuntimeServices* efiRtTable = nullptr;

    void TryEnableEfiRuntimeServices(const Loader::EfiDetails& details, 
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

        if (details.memmapSize == 0 || details.memmapDescSize > MaxDescSize 
            || details.memmapDescSize < sizeof(sl::EfiMemoryDescriptor)
            || details.memmapDescSize % alignof(sl::EfiMemoryDescriptor) != 0)
        {
            Log("EFI runtime services unavailable: bad memory map format",
                LogLevel::Error);

            return;
        }

        sl::EfiSystemTable sysTable {};
        if (CopyFromPhysical(details.systemTable, 
            { (char*)&sysTable, sizeof(sysTable) }) != sizeof(sysTable))
        {
            Log("EFI runtime services unavailable: failed to read system table",
                LogLevel::Error);

            return;
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

            return;
        }

        const size_t rtDescsLen = rtDescCount * details.memmapDescSize;
        const uintptr_t rtDescsBase = virtBase;
        for (size_t off = 0; off < rtDescsLen; 
            off += PageSize(), virtBase += PageSize())
        {
            auto* page = AllocPage(false);
            auto paddr = LookupPagePaddr(page);

            auto result = SetKernelMap(virtBase, paddr, VmFlag::Write);
            NPK_ASSERT(result == NpkStatus::Success); //TODO: non fatal handling
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
            const size_t bytes = desc->numberOfPages * PageSize();
            const bool isRuntime = 
                desc->attributes.Has(sl::EfiMemoryFlag::Runtime);
            const uintptr_t highBase = virtBase;

            for (size_t off = 0; off < bytes; off += PageSize())
            {
                const uintptr_t paddr = desc->physicalStart + off;
                auto result = SetKernelMap(paddr, paddr, rtFlags);

                if (result == NpkStatus::AlreadyMapped)
                {
                    //something is already mapped here, backup the address
                    //and then re-map it as we expect it.
                    //TODO: we should also backup the flags too!
                    Paddr prevPaddr = 0;
                    NPK_ASSERT(ClearKernelMap(paddr, &prevPaddr)
                        == NpkStatus::Success);
                    NPK_ASSERT(SetKernelMap(paddr, paddr, rtFlags)
                        == NpkStatus::Success);
                    NPK_ASSERT(prevPaddr == paddr);

                    //TODO: again, handle this gracefully
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

        NPK_ASSERT(rtVaddr != 0);
        HwFlushTlbAll();

        auto* rtIdent = reinterpret_cast<sl::EfiRuntimeServices*>(rtPaddr);
        auto result = rtIdent->SetVirtualAddressMap(
            static_cast<sl::EfiUintN>(rtDescsLen),
            static_cast<sl::EfiUintN>(details.memmapDescSize),
            details.memmapDescVersion,
            reinterpret_cast<sl::EfiMemoryDescriptor*>(rtDescsBase)
            );

        //remove identity map, taking care to preserve any pre-existing
        //mappings.
        for (size_t i = 0; i < descCount; i++)
        {
            auto* desc = ReadDesc(i);
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

        if (result != 0)
        {
            Log("EFI runtime services unavailable: SetVirtualAddressMap() "
                "returned %i", LogLevel::Error, result);

            return;
        }

        efiRtTable = reinterpret_cast<sl::EfiRuntimeServices*>(rtVaddr);
        Log("EFI runtime services available, table at %p", LogLevel::Info,
            efiRtTable);
    }

    sl::Opt<sl::EfiRuntimeServices*> GetEfiRtServices()
    {
        if (efiRtTable == nullptr)
            return {};
        return efiRtTable;
    }
}
