#include <private/Core.hpp>

namespace Npk
{
    enum class JacketedOp
    {
        Load,
        Store,
        Exchange,
        CompareExchange,
    };

    template<typename T>
    struct JacketedAtomicArgs
    {
        sl::Atomic<T>* target;
        T value;
        T out;
        JacketedOp op;
        sl::MemoryOrder order;
        sl::MemoryOrder failOrder;
        bool exchanged;
    };

    static NpkStatus CheckJacketMask(HwExceptionFlags flags)
    {
        if (!flags.Any())
            return NpkStatus::InvalidArg;

        const bool vmmMayRun = flags.Has(HwExceptionFlag::BadPageFault)
            && !flags.Has(HwExceptionFlag::PageFault);
        if (vmmMayRun && CurrentIpl() != Ipl::Passive)
            return NpkStatus::NotAvailable;

        return NpkStatus::Success;
    }

    NpkStatus JacketedMemCopy(HwExceptionFlags& flags, void* dest,
        const void* src, size_t len)
    {
        auto result = CheckJacketMask(flags);
        if (result != NpkStatus::Success)
            return result;
        if (dest == nullptr || src == nullptr)
            return NpkStatus::InvalidArg;

        struct Args
        {
            void* dest;
            const void* src;
            size_t length;
        };

        auto Inner = [](void* arg) -> void
        {
            auto* args = static_cast<Args*>(arg);

            sl::MemCopy(args->dest, args->src, args->length);
        };

        Args args
        {
            .dest = dest,
            .src = src,
            .length = len
        };

        const auto fired = HwExceptionJacket(Inner, &args, flags);
        flags = fired;

        if (flags.Any())
            return NpkStatus::InternalError;

        return NpkStatus::Success;
    }

    NpkStatus JacketedMemSet(HwExceptionFlags& flags, void* dest, int value,
        size_t len)
    {
        auto result = CheckJacketMask(flags);
        if (result != NpkStatus::Success)
            return result;
        if (dest == nullptr)
            return NpkStatus::InvalidArg;

        struct Args
        {
            void* dest;
            int value;
            size_t length;
        };

        auto Inner = [](void* arg) -> void
        {
            auto* args = static_cast<Args*>(arg);

            sl::MemSet(args->dest, args->value, args->length);
        };

        Args args
        {
            .dest = dest,
            .value = value,
            .length = len
        };

        const auto fired = HwExceptionJacket(Inner, &args, flags);
        flags = fired;

        if (flags.Any())
            return NpkStatus::InternalError;

        return NpkStatus::Success;
    }

    template<typename T>
    static NpkStatus RunJacketedAtomic(HwExceptionFlags& flags, void* out,
        void* target, const void* value, JacketedOp op,
        sl::MemoryOrder order)
    {
        auto Inner = [](void* arg) -> void
        {
            auto* args = static_cast<JacketedAtomicArgs<T>*>(arg);

            switch (args->op)
            {
            case JacketedOp::Load:
                args->out = args->target->Load(args->order);
                break;

            case JacketedOp::Store:
                args->target->Store(args->value, args->order);
                break;

            case JacketedOp::Exchange:
                args->out = args->target->Exchange(args->value, args->order);
                break;

            case JacketedOp::CompareExchange:
                {
                    args->exchanged = args->target->CompareExchange(
                        args->out, args->value, args->order);
                    break;
                }

            default:
                NPK_UNREACHABLE();
            }
        };

        sl::MemoryOrder failOrder = order;
        if (order == sl::Release)
            failOrder = sl::Relaxed;
        else if (order == sl::AcqRel)
            failOrder = sl::Acquire;

        JacketedAtomicArgs<T> args {};
        args.target = static_cast<sl::Atomic<T>*>(target);
        args.op = op;
        args.order = order;
        args.failOrder = failOrder;

        if (value != nullptr)
            sl::MemCopy(&args.value, value, sizeof(T));
        if (op == JacketedOp::CompareExchange)
            sl::MemCopy(&args.out, out, sizeof(T));

        flags = HwExceptionJacket(Inner, &args, flags);
        if (flags.Any())
            return NpkStatus::InternalError;

        if (out != nullptr)
            sl::MemCopy(out, &args.out, sizeof(T));

        if (op == JacketedOp::CompareExchange && !args.exchanged)
            return NpkStatus::ValueMismatch;

        return NpkStatus::Success;
    }

    static NpkStatus DoJacketedAtomic(HwExceptionFlags& flags, void* out,
        void* target, const void* value, size_t width, JacketedOp op,
        sl::MemoryOrder order)
    {
        auto result = CheckJacketMask(flags);
        if (result != NpkStatus::Success)
            return result;
        if (target == nullptr)
            return NpkStatus::InvalidArg;
        if (width == 0 || (width & (width - 1)) != 0 || width > 8)
            return NpkStatus::InvalidArg;

        const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);
        if ((targetAddr & (width - 1)) != 0)
            return NpkStatus::InvalidArg;

        switch (width)
        {
        case 1:
            return RunJacketedAtomic<uint8_t>(flags, out, target, value, op,
                order);
        case 2:
            return RunJacketedAtomic<uint16_t>(flags, out, target, value, op,
                order);
        case 4:
            return RunJacketedAtomic<uint32_t>(flags, out, target, value, op,
                order);
        case 8:
            if constexpr (SL_ATOMIC_ALWAYS_LOCK_FREE(8))
            {
                return RunJacketedAtomic<uint64_t>(flags, out, target, value,
                    op, order);
            }
            else
                return NpkStatus::Unsupported;

        default:
            NPK_UNREACHABLE();
        }
    }

    NpkStatus JacketedAtomicLoad(HwExceptionFlags& flags, void* out,
        const void* target, size_t width, sl::MemoryOrder order)
    {
        if (out == nullptr)
            return NpkStatus::InvalidArg;
        if (order == sl::Release || order == sl::AcqRel)
            return NpkStatus::InvalidArg;

        auto* mutableTarget = const_cast<void*>(target);

        return DoJacketedAtomic(flags, out, mutableTarget, nullptr, width,
            JacketedOp::Load, order);
    }

    NpkStatus JacketedAtomicStore(HwExceptionFlags& flags, void* target,
        const void* value, size_t width, sl::MemoryOrder order)
    {
        if (value == nullptr)
            return NpkStatus::InvalidArg;
        if (order == sl::Acquire || order == sl::AcqRel)
            return NpkStatus::InvalidArg;

        return DoJacketedAtomic(flags, nullptr, target, value, width,
            JacketedOp::Store, order);
    }

    NpkStatus JacketedAtomicExchange(HwExceptionFlags& flags, void* target,
        void* value, size_t width, sl::MemoryOrder order)
    {
        if (value == nullptr)
            return NpkStatus::InvalidArg;

        return DoJacketedAtomic(flags, value, target, value, width,
            JacketedOp::Exchange, order);
    }

    NpkStatus JacketedAtomicCompareExchange(HwExceptionFlags& flags,
        void* target, void* expected, const void* value, size_t width,
        sl::MemoryOrder order)
    {
        if (expected == nullptr || value == nullptr)
            return NpkStatus::InvalidArg;

        return DoJacketedAtomic(flags, expected, target, value, width,
            JacketedOp::CompareExchange, order);
    }
}
