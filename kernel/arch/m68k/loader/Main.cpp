#include "Memory.h"
#include "Util.h"
#include <Maths.h>

namespace Npl
{
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
        //TODO: create loader page tables, with hhdm/id map
        EnableMmu();

        Panic(PanicReason::KernelReturned);
    }
}
