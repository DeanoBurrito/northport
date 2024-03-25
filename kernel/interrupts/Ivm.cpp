#include <interrupts/Ivm.h>
#include <debug/Log.h>

namespace Npk::Interrupts
{
    IVM& IVM::Global()
    {}

    void IVM::Init()
    {
        ASSERT_UNREACHABLE();
    }

    size_t IVM::WindowSize() const
    {
        ASSERT_UNREACHABLE();
    }

    bool IVM::Claim(InterruptVector vector)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<InterruptVector> IVM::Alloc(size_t zone, sl::Opt<VectorPriority> priority)
    {
        ASSERT_UNREACHABLE();
    }

    void IVM::Free(InterruptVector vector)
    {
        ASSERT_UNREACHABLE();
    }

    bool IVM::Install(InterruptVector vector, VectorHandler handler)
    {
        ASSERT_UNREACHABLE();
    }

    bool IVM::Uninstall(InterruptVector vector)
    {
        ASSERT_UNREACHABLE();
    }
}
