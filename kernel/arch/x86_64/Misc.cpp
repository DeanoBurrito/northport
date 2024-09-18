#include <arch/Misc.h>

#include <core/Log.h>

namespace Npk
{
    void ExplodeKernelAndReset()
    {
        struct PACKED_STRUCT
        {
            uint16_t limit = 0;
            uint64_t base = 0;
        } idtr;

        asm volatile("lidt %0; int $0" :: "m"(idtr));
    }

    void InitExtendedRegs(ExtendedRegs** regs)
    { ASSERT_UNREACHABLE(); }

    void SaveExtendedRegs(ExtendedRegs* regs)
    { ASSERT_UNREACHABLE(); }

    void LoadExtendedRegs(ExtendedRegs* regs)
    { ASSERT_UNREACHABLE(); }

    bool ExtendedRegsFence()
    { ASSERT_UNREACHABLE(); }

    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t retAddr;
        };

        size_t count = 0;
        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));

        for (size_t i = 0; i < offset; i++)
        {
            if (current == 0)
                return count;
            current = current->next;
        }

        for (size_t i = 0; i < store.Size(); i++)
        {
            if (current == nullptr)
                return count;
            store[count++] = current->retAddr;
            current = current->next;
        }

        return count;
    }
}
