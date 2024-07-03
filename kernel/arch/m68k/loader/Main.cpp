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

    void Panic(PanicReason)
    {
        //leave d0 alone it should contain our error code
        asm("clr %d1; add #0xDEAD, %d1");
        asm("clr %d2; add #0xDEAD, %d2");
        asm("clr %d3; add #0xDEAD, %d3");
        while (true)
            asm("stop #0x2700");
        __builtin_unreachable();
    }

    sl::CNativePtr FindBootInfoTag(BootInfoType type, sl::CNativePtr begin)
    {
        constexpr size_t ReasonableSearchCount = 50;

        if (begin.ptr == nullptr)
            begin = sl::AlignUp((uintptr_t)LOADER_BLOB_END, 2);

        for (size_t i = 0; i < ReasonableSearchCount; i++)
        {
            auto tag = begin.As<BootInfoTag>();
            if (tag->type == BootInfoType::Last)
                return nullptr;
            if (tag->type == type)
                return begin;
            begin = begin.Offset(tag->size);
        }

        return nullptr;
    }

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
                auto fileDesc = new limine_file();
                fileDesc->revision = 0;
                //fileDesc->address = KERNEL_BLOB_BEGIN;
                //fileDesc->size = (size_t)KERNEL_BLOB_END - (size_t)KERNEL_BLOB_BEGIN;
                fileDesc->path = EmptyStr;
                fileDesc->cmdline = NPL_KERNEL_CMDLINE;

                auto resp = new limine_kernel_file_response();
                resp->revision = 0;
                resp->kernel_file = fileDesc;

                //req->response = resp;
            }
        },
        {
            .id = LIMINE_MODULE_REQUEST,
            .Handle = [](LbpRequest* req)
            {
                const auto maybeInitRd = FindBootInfoTag(BootInfoType::InitRd);
                if (maybeInitRd.ptr == nullptr)
                    return;

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

        LoadKernel();

        //memory map request gets special treatment: we want to handle it last since we
        //may modify the memory map while handling other responses.
        const uint64_t mmapId[4] = LIMINE_MEMMAP_REQUEST;
        LbpRequest* mmapRequest = nullptr;

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

                RequestHandlers[i].Handle(request);
                break;
            }
        }

        if (mmapRequest != nullptr)
        {
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
                if (entryCount <= storeCount)
                    break;

                delete[] entries;
                delete[] entryPtrs;
                entries = nullptr;
                entryPtrs = nullptr;
            }

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
