#include "Loader.h"
#include "Memory.h"

//bit of assembly in the global namespace - you love to see it 8D
asm("\
.section .rodata \n\
.balign 0x10 \n\
KERNEL_BLOB_BEGIN: \n\
    .incbin \"build/kernel.elf\" \n\
KERNEL_BLOB_END: \n\
.previous \n\
");

namespace Npl
{
    bool LoadKernel()
    {}

    bool PopulateResponses()
    {}

    void ExecuteKernel()
    {}
}
