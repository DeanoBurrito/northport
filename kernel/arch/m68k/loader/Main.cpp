#include "Memory.h"
#include "Util.h"
#include "Loader.h"
#include <Maths.h>
#include <boot/Limine.h>
#include <Memory.h>

#ifndef NPL_KERNEL_CMDLINE
    #define NPL_KERNEL_CMDLINE ""
#endif

#ifndef NPL_INITRD_CMDLINE
    #define NPL_INITRD_CMDLINE ""
#endif

namespace Npl
{
    const char* EmptyStr = "" + HhdmBase;

    struct LbpRequestHandler
    {
        uint64_t id[4];
        void (*Handle)(LbpRequest* req);
    };

    constexpr LbpRequestHandler RequestHandlers[] =
    {
        {
            .id = LIMINE_BOOTLOADER_INFO_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                NPL_LOG("Populating bootloader info response.\r\n");
                auto resp = new limine_bootloader_info_response();
                resp->revision = 0;
                resp->name = "northport m68k loader";
                resp->version = "1";

                resp->name += HhdmBase;
                resp->version += HhdmBase;

                req->response = resp;
            }
        },
        {
            .id = LIMINE_HHDM_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                NPL_LOG("Populating HHDM response.\r\n");
                auto resp = new limine_hhdm_response();
                resp->revision = 0;
                resp->offset = HhdmBase;

                req->response = resp;
            }
        },
        {
            .id = LIMINE_SMP_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                NPL_LOG("Populating SMP response.\r\n");
                auto resp = new limine_smp_response();
                resp->revision = 0;
                resp->flags = 0;
                resp->bsp_id = 0;
                resp->cpu_count = 1;

                auto info = new limine_smp_info();
                info->id = 0;
                info->processor_id = 0;

                auto infoBlock = new limine_smp_info*[1]; //lol
                infoBlock[0] = info;
                resp->cpus = infoBlock;

                req->response = resp;
            }
        },
        {
            .id = LIMINE_KERNEL_FILE_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                NPL_LOG("Populating kernel file response.\r\n");
                auto fileDesc = new limine_file();
                fileDesc->revision = 0;
                fileDesc->address = reinterpret_cast<LIMINE_PTR(void*)>((uintptr_t)KERNEL_BLOB_BEGIN + HhdmBase);
                fileDesc->size = reinterpret_cast<uint64_t>(KERNEL_BLOB_END);
                fileDesc->size -= reinterpret_cast<uint64_t>(KERNEL_BLOB_BEGIN); 
                fileDesc->path = EmptyStr;

                const char bakedCmdline[] = NPL_KERNEL_CMDLINE;
                const char* liveCmdline = nullptr;
                size_t liveLen = 0;
                if (auto found = FindBootInfoTag(BootInfoType::CommandLine); found.ptr != nullptr)
                {
                    liveCmdline = found.Offset(sizeof(BootInfoTag)).As<const char>();
                    liveLen = found.As<BootInfoTag>()->size;
                }

                char* cmdline = static_cast<char*>(AllocGeneral(sizeof(bakedCmdline) + liveLen));
                sl::memcopy(bakedCmdline, cmdline, sizeof(bakedCmdline));
                sl::memcopy(liveCmdline, cmdline + (sizeof(bakedCmdline) - 1), liveLen);
                fileDesc->cmdline = cmdline;

                auto resp = new limine_kernel_file_response();
                resp->revision = 0;
                resp->kernel_file = fileDesc;

                req->response = resp;
            }
        },
        {
            .id = LIMINE_MODULE_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                const auto maybeInitRd = FindBootInfoTag(BootInfoType::InitRd);
                if (maybeInitRd.ptr == nullptr)
                {
                    NPL_LOG("No initrd passed to loader, ignoring modules request.\r\n");
                    return;
                }

                NPL_LOG("Populating modules response (with initrd).\r\n");
                const auto initrd = maybeInitRd.Offset(sizeof(BootInfoTag)).As<BootInfoMemChunk>();
                auto file = new limine_file();
                file->revision = 0;
                file->address = reinterpret_cast<LIMINE_PTR(void*)>(initrd->addr + HhdmBase);
                file->size = initrd->size;
                file->path = EmptyStr;

                const char initrdCmdline[] = NPL_INITRD_CMDLINE;
                char* cmdline = new char[sizeof(initrdCmdline)];
                sl::memcopy(initrdCmdline, cmdline, sizeof(initrdCmdline));
                file->cmdline = cmdline;

                auto infoBlock = new limine_file*[1];
                infoBlock[0] = file;

                auto resp = new limine_module_response();
                resp->revision = 0;
                resp->module_count = 1;
                resp->modules = infoBlock;

                req->response = resp;
            }
        },
        {
            .id = LIMINE_KERNEL_ADDRESS_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                NPL_LOG("Populating kernel address response.\r\n");
                auto resp = new limine_kernel_address_response();
                resp->revision = 0;
                GetKernelBases(&resp->physical_base, &resp->virtual_base);

                req->response = resp;
            }
        }
    };
}

extern "C"
{
    uintptr_t __stack_chk_guard = static_cast<uintptr_t>(0x57656C2C6675636B);

    void __stack_chk_fail()
    { Npl::Panic(Npl::PanicReason::StackCheckFail); }

    void LoaderEntryNext()
    {
        using namespace Npl;

#ifdef NPL_ENABLE_LOGGING
        auto maybeUart = FindBootInfoTag(BootInfoType::GoldfishTtyBase);
        if (maybeUart.ptr != nullptr)
        {
            uart = maybeUart.Offset(sizeof(BootInfoTag)).Read<uint32_t>();
            uart.Offset(8).Write<uint32_t>(0); //disable interrupts
            NPL_LOG("\r\nNorthport m68k loader starting ...\r\n");
            NPL_LOG("UART enabled, mapped at %p\r\n", uart.ptr);
        }
#endif
        InitMemoryManager();

        const size_t hhdmLimit = HhdmLimit();
        for (size_t i = PageSize; i < hhdmLimit; i += PageSize)
        {
            if (!MapMemory(PageSize, i, i))
                Panic(PanicReason::HhdmSetupFail);
            if (!MapMemory(PageSize, i + HhdmBase, i))
                Panic(PanicReason::HhdmSetupFail);
        }

        EnableMmu();

#ifdef NPL_ENABLE_LOGGING
        uart = MapMemory(PageSize, hhdmLimit, maybeUart.Offset(sizeof(BootInfoTag)).Read<uint32_t>());
        NPL_LOG("MMU enabled, UART re-mapped at %p\r\n", uart.ptr);
#endif

        LoadKernel();

        //memory map request gets special treatment: we want to handle it last since we
        //may modify the memory map while handling other responses.
        const uint64_t mmapId[4] = LIMINE_MEMMAP_REQUEST;
        LbpRequest* mmapRequest = nullptr;

        NPL_LOG("Handling LBP requests ...\r\n");
        for (LbpRequest* request = LbpNextRequest(); request != nullptr; request = LbpNextRequest(request))
        {
            if (sl::memcmp(mmapId, request->id, 32) == 0)
            {
                mmapRequest = request;
                continue;
            }

            for (size_t i = 0; i < sizeof(RequestHandlers) / sizeof(LbpRequestHandler); i++)
            {
                if (sl::memcmp(RequestHandlers[i].id, request->id, 32) != 0)
                    continue;

                NPL_LOG("Found request at %p\r\n", request);
                RequestHandlers[i].Handle(request);
                break;
            }
        }

        if (mmapRequest != nullptr)
        {
            NPL_LOG("Handling memory map request ...\r\n");
            constexpr size_t MaxMemmapAllocTries = 3;

            limine_memmap_response* resp = new limine_memmap_response();
            limine_memmap_entry* entries = nullptr;
            limine_memmap_entry** entryPtrs = nullptr;
            size_t entryCount = 0;

            for (size_t i = 0; i < MaxMemmapAllocTries; i++)
            {
                const size_t tentativeCount = GenerateLbpMemoryMap(nullptr, 0);
                const size_t storeCount = tentativeCount + tentativeCount / 2;
                entries = new limine_memmap_entry[storeCount];
                entryPtrs = new limine_memmap_entry*[storeCount];
                
                entryCount = GenerateLbpMemoryMap(entries, storeCount);
                NPL_LOG("Memory map finalize attempt: store=%u, actual=%u\r\n", storeCount, entryCount);
                if (entryCount <= storeCount)
                    break;

                NPL_LOG("Finalize attempt failed, retrying\r\n");
                delete[] entries;
                delete[] entryPtrs;
                entries = nullptr;
                entryPtrs = nullptr;
            }
            NPL_LOG("Finalized memory map, populating protocol response.\r\n");

            for (size_t i = 0; i < entryCount; i++)
                entryPtrs[i] = &entries[i];
            resp->revision = 0;
            resp->entry_count = entryCount;
            resp->entries = entryPtrs;

            mmapRequest->response = resp;
        }

        ExecuteKernel();
        Panic(PanicReason::KernelReturned);
    }
}
