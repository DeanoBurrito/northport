#include <debug/Log.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/Helpers.h>
#include <tasking/RunLevels.h>
#include <interrupts/Router.h>

extern "C"
{
    using namespace Npk;
    using namespace Npk::Tasking;
    using namespace Npk::Interrupts;

    static_assert((size_t)RunLevel::Normal == npk_runlevel::Normal);
    static_assert((size_t)RunLevel::Apc == npk_runlevel::Apc);
    static_assert((size_t)RunLevel::Dpc == npk_runlevel::Dpc);
    static_assert((size_t)RunLevel::Clock == npk_runlevel::Clock);
    static_assert((size_t)RunLevel::Interrupt == npk_runlevel::Interrupt);

    static_assert(sizeof(npk_dpc) == sizeof(DpcStore));
    //static_assert(alignof(npk_dpc) == alignof(DpcStore));
    static_assert(offsetof(npk_dpc, reserved) == offsetof(DpcStore, next));
    static_assert(offsetof(npk_dpc, function) == offsetof(DpcStore, data.function));
    static_assert(offsetof(npk_dpc, arg) == offsetof(DpcStore, data.arg));

    static_assert(sizeof(npk_apc) == sizeof(ApcStore));
    //static_assert(alignof(npk_apc) == alignof(ApcStore));
    static_assert(offsetof(npk_apc, reserved) == offsetof(ApcStore, next));
    static_assert(offsetof(npk_apc, function) == offsetof(ApcStore, data.function));
    static_assert(offsetof(npk_apc, arg) == offsetof(ApcStore, data.arg));
    static_assert(offsetof(npk_apc, thread_id) == offsetof(ApcStore, data.threadId));

    using IntrRoute = Interrupts::InterruptRoute;
    static_assert(sizeof(npk_interrupt_route) == sizeof(IntrRoute));
    static_assert(alignof(npk_interrupt_route) == alignof(IntrRoute));
    static_assert(offsetof(npk_interrupt_route, callback_arg) == offsetof(IntrRoute, callbackArg));
    static_assert(offsetof(npk_interrupt_route, callback) == offsetof(IntrRoute, Callback));
    static_assert(offsetof(npk_interrupt_route, dpc) == offsetof(IntrRoute, dpc));

    DRIVER_API_FUNC
    bool npk_ensure_runlevel(npk_runlevel rl, REQUIRED npk_runlevel* prev)
    {
        VALIDATE_(prev != nullptr, false);

        auto result = EnsureRunLevel(static_cast<RunLevel>(rl));
        if (!result.HasValue())
            return false;

        *prev = static_cast<npk_runlevel>(*result);
        return true;
    }

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
    void npk_queue_dpc(REQUIRED npk_dpc* dpc)
    {
        VALIDATE_(dpc != nullptr, );
        VALIDATE_(dpc->function != nullptr, );

        QueueDpc(reinterpret_cast<DpcStore*>(dpc));
    }

    DRIVER_API_FUNC
    void npk_queue_remote_dpc(REQUIRED npk_dpc* dpc, npk_core_id core)
    {
        VALIDATE_(dpc != nullptr, );
        VALIDATE_(dpc->function != nullptr, );

        QueueRemoteDpc(core, reinterpret_cast<DpcStore*>(dpc));
    }

    DRIVER_API_FUNC
    void npk_queue_apc(REQUIRED npk_apc* apc)
    {
        VALIDATE_(apc != nullptr,);
        VALIDATE_(apc->function != nullptr,);

        QueueApc(reinterpret_cast<ApcStore*>(apc));
    }
    
    DRIVER_API_FUNC
    bool npk_add_interrupt_route(REQUIRED npk_interrupt_route* route, npk_core_id core)
    {
        VALIDATE_(route != nullptr, false);
        if (core == NPK_CURRENT_AFFINITY)
            core = CoreLocal().id;

        return AddInterruptRoute(reinterpret_cast<InterruptRoute*>(route), core);
    }

    DRIVER_API_FUNC
    bool npk_claim_interrupt_route(REQUIRED npk_interrupt_route* route, npk_core_id core, size_t gsi)
    {
        VALIDATE_(route != nullptr, false);
        if (core == NPK_CURRENT_AFFINITY)
            core = CoreLocal().id;

        return ClaimInterruptRoute(reinterpret_cast<InterruptRoute*>(route), core, gsi);
    }

    DRIVER_API_FUNC
    bool npk_remove_interrupt_route(REQUIRED npk_interrupt_route* route)
    {
        VALIDATE_(route != nullptr, false);

        return RemoveInterruptRoute(reinterpret_cast<InterruptRoute*>(route));
    }

    DRIVER_API_FUNC
    bool npk_construct_msi(REQUIRED npk_interrupt_route* route, REQUIRED npk_msi_config* cfg)
    {
        VALIDATE_(route != nullptr, false);
        VALIDATE_(cfg != nullptr, false);

        auto msi = ConstructMsi(reinterpret_cast<InterruptRoute*>(route));
        VALIDATE_(msi.HasValue(), false);

        cfg->address = msi->address;
        cfg->data = msi->data;
        return true;
    }
}
