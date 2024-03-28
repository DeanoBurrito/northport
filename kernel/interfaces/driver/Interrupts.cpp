#include <debug/Log.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/Helpers.h>
#include <tasking/RunLevels.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Tasking;

    static_assert((size_t)RunLevel::Normal == npk_runlevel::Normal);
    static_assert((size_t)RunLevel::Apc == npk_runlevel::Apc);
    static_assert((size_t)RunLevel::Dpc == npk_runlevel::Dpc);
    static_assert((size_t)RunLevel::Clock == npk_runlevel::Clock);
    static_assert((size_t)RunLevel::Interrupt == npk_runlevel::Interrupt);

    static_assert(sizeof(npk_dpc) == sizeof(DpcStore));
    static_assert(alignof(npk_dpc) == alignof(DpcStore));
    static_assert(offsetof(npk_dpc, reserved) == offsetof(DpcStore, next));
    static_assert(offsetof(npk_dpc, function) == offsetof(DpcStore, data.function));
    static_assert(offsetof(npk_dpc, arg) == offsetof(DpcStore, data.arg));

    static_assert(sizeof(npk_apc) == sizeof(ApcStore));
    static_assert(alignof(npk_apc) == alignof(ApcStore));
    static_assert(offsetof(npk_apc, reserved) == offsetof(ApcStore, next));
    static_assert(offsetof(npk_apc, function) == offsetof(ApcStore, data.function));
    static_assert(offsetof(npk_apc, arg) == offsetof(ApcStore, data.arg));
    static_assert(offsetof(npk_apc, thread_id) == offsetof(ApcStore, data.threadId));

    DRIVER_API_FUNC
    npk_runlevel npk_raise_runlevel(npk_runlevel rl)
    {
        return static_cast<npk_runlevel>(RaiseRunLevel(static_cast<RunLevel>(rl)));
    }

    DRIVER_API_FUNC
    void npk_lower_runlevel(npk_runlevel rl)
    {
        LowerRunLevel(static_cast<RunLevel>(rl));
    }

    DRIVER_API_FUNC
    void npk_queue_dpc(npk_dpc* dpc)
    {
        VALIDATE_(dpc != nullptr,);
        VALIDATE_(dpc->function != nullptr,);

        QueueDpc(reinterpret_cast<DpcStore*>(dpc));
    }

    DRIVER_API_FUNC
    void npk_queue_apc(npk_apc* apc)
    {
        VALIDATE_(apc != nullptr,);
        VALIDATE_(apc->function != nullptr,);

        QueueApc(reinterpret_cast<ApcStore*>(apc));
    }
}
