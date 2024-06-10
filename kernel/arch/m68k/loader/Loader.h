#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Npl
{
    bool LoadKernel();
    void ExecuteKernel();
    void GetKernelBases(uint64_t* phys, uint64_t* virt);

    struct LbpRequest
    {
        uint64_t id[4];
        uint64_t revision;
        union
        {
            uint64_t pad;
            void* response;
        };
    };

    LbpRequest* LbpNextRequest(LbpRequest* current = nullptr);
}
