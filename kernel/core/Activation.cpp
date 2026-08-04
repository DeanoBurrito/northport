#include <private/Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    constexpr HeapTag ActivationHeapTag = NPK_MAKE_HEAP_TAG("Actv");

    NpkStatus CreateActivation(Activation** outAct, sl::Opt<uintptr_t> entry, 
        sl::Opt<uintptr_t> stack, sl::Span<uintptr_t> args)
    {
        if (outAct == nullptr)
            return NpkStatus::InvalidArg;
        if (args.Size() > MaxActivationArgs)
            return NpkStatus::InvalidArg;

        void* hwPrivate;
        auto result = HwCreateUserActivation(&hwPrivate);
        if (result != NpkStatus::Success)
            return result;

        void* ptr = PoolAllocPaged(sizeof(Activation), ActivationHeapTag);
        if (ptr == nullptr)
        {
            result = HwDestroyUserActivation(hwPrivate);
            if (result != NpkStatus::Success)
                NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

            return NpkStatus::Shortage;
        }

        auto* activation = new(ptr) Activation {};
        activation->hwPrivate = hwPrivate;
        if (entry.HasValue())
            activation->entry = *entry;
        if (stack.HasValue())
            activation->stack = *stack;
        for (size_t i = 0; i < args.Size(); i++)
            activation->args[i] = args[i];

        *outAct = activation;

        return NpkStatus::Success;
    }

    NpkStatus DestroyActivation(Activation* act)
    {
        if (act == nullptr)
            return NpkStatus::InvalidArg;
        if (act->linked)
            return NpkStatus::InvalidArg;

        auto result = HwDestroyUserActivation(act->hwPrivate);
        if (result != NpkStatus::Success)
            return result;

        result = PoolFreePaged(act, sizeof(Activation), ActivationHeapTag);
        
        return result;
    }

    NpkStatus PushActivation(Activation& act, HwUserContext& context)
    {
        if (act.linked)
            return NpkStatus::InvalidArg;

        auto& list = HwGetUserContextActivations(context);

        list.PushFront(&act);
        act.linked = true;

        return NpkStatus::Success;
    }

    NpkStatus PopActivation(Activation** outAct, HwUserContext& context)
    {
        if (outAct == nullptr)
            return NpkStatus::InvalidArg;

        auto& list = HwGetUserContextActivations(context);

        auto* activation = list.PopFront();
        if (activation == nullptr)
            return NpkStatus::NotAvailable;
        NPK_ASSERT(activation->linked);
        activation->linked = false;

        *outAct = activation;

        return NpkStatus::Success;
    }

    NpkStatus PeekActivation(Activation** outAct, HwUserContext& context)
    {
        if (outAct == nullptr)
            return NpkStatus::InvalidArg;

        auto& list = HwGetUserContextActivations(context);

        if (list.Empty())
            return NpkStatus::NotAvailable;

        *outAct = &list.Front();
        NPK_ASSERT((*outAct)->linked);

        return NpkStatus::Success;
    }
}
