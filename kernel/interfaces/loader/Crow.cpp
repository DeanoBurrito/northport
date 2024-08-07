#include <interfaces/loader/Generic.h>
#include <interfaces/loader/Crow.h>
#include <arch/Platform.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <Maths.h>

namespace Npk
{
    enum ReqIndex
    {
        LoaderInfo = 0,
        Hhdm = 1,
        Display = 2,
        Platform = 3,
        Pagesize = 4,
        Firmware = 5,
        Memmap = 6,
        SymTable = 7,
        Time = 8,
    };

    static crow_request_t requests[] =
    {
        { .id = CROW_REQ_BLDR, .response = nullptr },
        { .id = CROW_REQ_HHDM, .response = nullptr },
        { .id = CROW_REQ_DISP, .response = nullptr },
        { .id = CROW_REQ_PLAT, .response = nullptr },
        { .id = CROW_REQ_PGSZ, .response = nullptr },
        { .id = CROW_REQ_FW, .response = nullptr },
        { .id = CROW_REQ_MMAP, .response = nullptr },
        { .id = CROW_REQ_SYMT, .response = nullptr },
        { .id = CROW_REQ_TIME, .response = nullptr },
        { .id = { .value = 0 }, .response = nullptr }
    };

    static crow_boot_info_t info = 
    {
        .requests = requests,
        .num_requests = sizeof(requests) / sizeof(crow_request_t),
        .kernel_entry = KernelEntry,
    };

    void ValidateLoaderData()
    {}
    
    bool GetHhdmBounds(uintptr_t& base, size_t& length)
    {
        if (requests[ReqIndex::Hhdm].response == nullptr 
            || requests[ReqIndex::Memmap].response == nullptr)
            return false;

        base = static_cast<crow_hhdm_info_t*>(requests[ReqIndex::Hhdm].response)->base_address;
        length = 0;

        auto mmapResp = static_cast<crow_memory_map_t*>(requests[ReqIndex::Memmap].response);
        for (size_t i = 0; i < mmapResp->num_entries; i++)
        {
            auto entry = mmapResp->entries[i];
            switch (entry.type)
            {
            case CROW_MEMMAP_TYPE_USABLE:
            case CROW_MEMMAP_TYPE_BOOTLOADER_RECLAIMABLE:
            case CROW_MEMMAP_TYPE_RAMDISK:
            case CROW_MEMMAP_TYPE_FRAMEBUFFER:
                break;
            default:
                continue;
            }

            length = sl::AlignUp(entry.start_addr + entry.size, PageSize);
        }

        ASSERT_(length != 0);
        return true;
    }

    uintptr_t GetKernelPhysAddr()
    {
        ASSERT_UNREACHABLE();
    }

    static size_t GetMemmapEntries(sl::Span<MemmapEntry> entries, size_t offset, uint64_t type)
    {
        if (requests[ReqIndex::Memmap].response == nullptr)
            return 0;

        auto mmapResp = static_cast<crow_memory_map_t*>(requests[ReqIndex::Memmap].response);
        size_t entryHead = 0;
        for (size_t i = 0; i < mmapResp->num_entries; i++)
        {
            const auto& entry = mmapResp->entries[i];
            if (entry.type != type)
                continue;
            if (i < offset)
                continue;

            if (entryHead == entries.Size())
                break;

            MemmapEntry& store = entries[entryHead++];
            store.base = entry.start_addr;
            store.length = entry.size;
        }

        return entryHead;
    }

    size_t GetUsableMemmap(sl::Span<MemmapEntry> entries, size_t offset)
    { return GetMemmapEntries(entries, 0, CROW_MEMMAP_TYPE_USABLE); }

    size_t GetReclaimableMemmap(sl::Span<MemmapEntry> entries, size_t offset)
    { return GetMemmapEntries(entries, offset, CROW_MEMMAP_TYPE_BOOTLOADER_RECLAIMABLE); } //TODO: we want to reclaim other areas, like initrd

    sl::Opt<uintptr_t> GetRsdp()
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<uintptr_t> GetDtb()
    {
        ASSERT_UNREACHABLE();
    }

    sl::Span<uint8_t> GetInitdisk()
    {
        ASSERT_UNREACHABLE();
    }

    size_t StartupAps()
    {
        ASSERT_UNREACHABLE();
    }

    sl::StringSpan GetCommandLine()
    {
        ASSERT_UNREACHABLE();
    }

    size_t GetFramebuffers(sl::Span<LoaderFramebuffer> fbs, size_t offset)
    {
        ASSERT_UNREACHABLE();
    }
}

extern "C" crow_boot_info_t* QueryEntrypoint()
{
    return &Npk::info;
}
