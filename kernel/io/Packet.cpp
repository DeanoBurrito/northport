#include <private/Io.hpp>
#include <lib/Maths.hpp>

namespace Npk
{
    union TerminatingData
    {
        size_t code;
        void* pointer;
    };

    //returns whether this function call successfully set the terminal
    //(meaning complete or abort) status on an iop, and was able to set the 
    //corresponding data field. If false is returned, someone else did it first.
    static bool TrySetIopTerminalStatus(Iop* packet, IoStatus status, 
        TerminatingData data)
    {
        NPK_ASSERT(status == IoStatus::Complete || status == IoStatus::Abort);

        IoStatus expected = IoStatus::Pending;
        IoStatus desired = IoStatus::Terminating;

        if (!packet->status.CompareExchange(expected, desired, sl::AcqRel))
            return false;

        if (status == IoStatus::Complete)
            packet->completionData.Store(data.pointer, sl::Relaxed);
        else if (status == IoStatus::Abort)
            packet->abortCode.Store(data.code, sl::Relaxed);

        packet->status.Store(status, sl::Release);

        return true;
    }

    NpkStatus AllocIoBuffers(IoBuffer** result, size_t count, bool wired)
    {
        const size_t allocLen = count * sizeof(IoBuffer);

        void* ptr = PoolAlloc(allocLen, Private::IoHeapTag, wired);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        IoBuffer* buff = new(ptr) IoBuffer {};
        if (wired)
            buff->flags.Set(IoBufferFlag::WiredStruct);

        for (size_t i = 1; i < count; i++)
        {
            auto* buffi = new(buff + i) IoBuffer {};
            buffi->flags = buff->flags;
        }

        *result = buff;
        return NpkStatus::Success;
    }

    NpkStatus FreeIoBuffers(IoBuffer* buffers, size_t count)
    {
        const size_t allocLen = count * sizeof(IoBuffer);
        const bool isWired = buffers->flags.Has(IoBufferFlag::WiredStruct);

        PoolFree(buffers, allocLen, Private::IoHeapTag, isWired);

        return NpkStatus::Success;
    }

    NpkStatus CreateIop(Iop** result, IoInterface* target, IoType type, 
        IoFlags flags, IopParams* params, sl::Span<IoBuffer> buffers)
    {
        if (result == nullptr)
            return NpkStatus::InvalidArg;
        if (target == nullptr)
            return NpkStatus::InvalidArg;
        if (params == nullptr)
            return NpkStatus::InvalidArg;

        if (!Private::RefIoInterface(target))
            return NpkStatus::ObjRefFailed;
        //since an IOI has an implied reference to their parent IOI, by holding
        //a reference to an IOI we're implicitly reference their parents,
        //so this single refcount is enough. This assumes no IOIs are ever moved
        //in the tree, which is something we dont support for now.

        size_t frameCount = 1;
        auto scan = target->parent;
        while (scan != nullptr)
        {
            frameCount++;
            scan = scan->parent;
        }

        NPK_ASSERT(frameCount < (1u << (sizeof(Iop::frameCount) * 8)));

        const size_t allocSize = sizeof(Iop) + frameCount * sizeof(IopFrame);

        void* ptr = PoolAlloc(allocSize, Private::IoHeapTag, 
            flags.Has(IopFlag::Wired));
        if (ptr == nullptr)
        {
            Private::UnrefIoInterface(target);

            return NpkStatus::Shortage;
        }

        auto* iop = new(ptr) Iop {};
        iop->type = type;
        iop->goingDown = true;
        iop->frameCount = frameCount;
        iop->frameCapacity = frameCount; //TODO: bake in some extra space.
        iop->frameIndex = 0;
        iop->status = IoStatus::Initialized;
        iop->flags.Store(flags, sl::Relaxed);
        iop->frames[0].params = *params;
        //TODO: continuation
        iop->buffers = buffers;
        
        auto* frameTarget = target;
        for (size_t i = 0; i < frameCount; i++)
        {
            //shouldn't ever happen, means IOI tree corruption.
            NPK_ASSERT(frameTarget != nullptr);

            auto& frame = iop->frames[i];
            frame.interface = frameTarget;

            frameTarget = frameTarget->parent;
        }

        *result = iop;
        return NpkStatus::Success;
    }

    NpkStatus DestroyIop(Iop* packet)
    {
        if (packet == nullptr)
            return NpkStatus::InvalidArg;
        auto status = packet->status.Load(sl::Relaxed);
        if (status != IoStatus::Complete && status != IoStatus::Abort)
            return NpkStatus::InUse;

        //wait for an active ProgressIop() call on this IOP to finish if
        //ongoing.
        while (packet->flags.Load(sl::Acquire).Has(IopFlag::Busy))
            sl::HintSpinloop();

        IoInterface* target = packet->frames[0].interface;

        bool isWired = packet->flags.Load(sl::Relaxed).Has(IopFlag::Wired);
        const size_t allocSize = sizeof(Iop) + packet->frameCapacity 
            * sizeof(IopFrame);
        const bool success = PoolFree(packet, allocSize, Private::IoHeapTag, 
                isWired);

        if (!success)
            return NpkStatus::InternalError;
        Private::UnrefIoInterface(target);

        return NpkStatus::Success;
    }

    NpkStatus StartIop(Iop* packet, bool waitUntilComplete)
    {
        if (packet == nullptr)
            return NpkStatus::InvalidArg;

        if (packet->flags.Load(sl::Acquire).Has(IopFlag::EmitTraces))
        {
            Log("Attempting to start IOP %p, %u frames", LogLevel::Trace, 
                packet, packet->frameCount);
            //TODO: print frames[0] arguments

            for (size_t i = 0; i < packet->frameCount; i++)
            {
                auto& frame = packet->frames[i];
                Log("IOP %p frame %zu: interface=%p", LogLevel::Trace,
                    packet, i, frame.interface);
            }

            for (size_t i = 0; i < packet->buffers.Size(); i++)
            {
                auto& buffer = packet->buffers[i];
                switch (buffer.type)
                {
                case IoBufferType::None:
                    Log("IOP %p buffer %zu: type=none", LogLevel::Trace,
                        packet, i);
                    break;

                case IoBufferType::KernelVirtual:
                    Log("IOP %p buffer %zu: type=kernel, flags=%lx, offset=%zu,"
                        " len=%zu, base=%tx", LogLevel::Trace,
                        packet, i, buffer.flags.value, buffer.offset,
                        buffer.length, buffer.kernel.base);
                    break;

                case IoBufferType::UserVirtual:
                    Log("IOP %p buffer %zu: type=user, flags=%lx, offset=%zu, "
                        "len=%zu, base=%tx, space=%p", LogLevel::Trace,
                        packet, i, buffer.flags.value, buffer.offset,
                        buffer.length, buffer.user.base, buffer.user.space);
                    break;

                case IoBufferType::PageList:
                    Log("IOP %p buffer %zu: type=pagelist, flags=%lx, "
                        "offset=%zu, len=%zu, count=%zu", LogLevel::Trace,
                        packet, i, buffer.flags.value, buffer.offset,
                        buffer.length, buffer.physical.pages.Size());
                    break;
                }
            }
        }
        
        IoStatus expected = IoStatus::Initialized;
        IoStatus desired = IoStatus::Pending;
        if (!packet->status.CompareExchange(expected, desired, sl::AcqRel))
            return NpkStatus::InUse;

        auto result = ProgressIop(packet);
        if (!waitUntilComplete)
            return result;

        return RunIopUntilComplete(packet, false);
    }

    NpkStatus ProgressIop(Iop* packet)
    {
        if (CurrentIpl() != Ipl::Passive)
            return NpkStatus::NotAvailable;
        if (packet == nullptr)
            return NpkStatus::InvalidArg;
        if (packet->status.Load(sl::Acquire) == IoStatus::Initialized)
            return NpkStatus::InvalidArg;

        //enforce single-caller invariant
        while (true)
        {
            auto flags = packet->flags.Load(sl::Acquire);
            if (flags.Has(IopFlag::Busy))
                return NpkStatus::Busy;

            auto desired = flags | IopFlag::Busy;
            if (packet->flags.CompareExchange(flags, desired, sl::Acquire))
                break;

            //try again, CompareExchange fails when any flag bit is changed,
            //which was not necessarily IopFlag::Busy.
        }

        auto ClearBusyFlag = [&]() -> void
        {
            auto expected = packet->flags.Load(sl::Relaxed);
            decltype(expected) desired;

            do
            {
                desired = expected;
                desired.Clear(IopFlag::Busy);
            }
            while (!packet->flags.CompareExchange(expected, desired, 
                sl::Release));
        };

        const bool emitTraces = 
            packet->flags.Load(sl::Relaxed).Has(IopFlag::EmitTraces);
        if (emitTraces)
        {
            Log("Attempting to progress IOP %p, %u/%u %s", LogLevel::Trace,
                packet, packet->frameIndex + 1, packet->frameCount, 
                packet->goingDown ? "down" : "up");
        }

        Condition* completeCondVar = packet->completeCondition;
        IopCompletionCallback completeCallback = packet->completeCallback;
        void* completeCbArg = packet->callbackOpaque;

        bool terminate = false;
        while (!terminate)
        {
            if (packet->status.Load(sl::Acquire) != IoStatus::Pending)
            {
                if (emitTraces)
                {
                    auto status = packet->status.Load(sl::Acquire);
                    Log("IOP %p has non-pending status %u (%s), begin abort.",
                        LogLevel::Trace, packet, status, 
                        IoStatusStr(status).Begin());
                }

                packet->goingDown = false;
            }

            NPK_ASSERT(packet->frameIndex < packet->frameCount);
            auto& frame = packet->frames[packet->frameIndex];
            NPK_ASSERT(frame.interface != nullptr);

            if (packet->goingDown)
            {
                NPK_ASSERT(frame.interface->Begin != nullptr);

                if (emitTraces)
                {
                    Log("IOP %p calling Begin() %u/%u: %p", LogLevel::Trace,
                        packet, packet->frameIndex + 1, packet->frameCount,
                        frame.interface->Begin);
                }
                auto status = frame.interface->Begin(packet,
                    frame.interface->opaque, &frame.stash);
                if (emitTraces)
                {
                    Log("IOP %p Begin() call %u/%u returned %u (%s)",
                        LogLevel::Trace, packet, packet->frameIndex + 1,
                        packet->frameCount, status, 
                        IoStatusStr(status).Begin());
                }

                switch (status)
                {
                /* The device needs more time, leave the IOP as-is and return
                 * a busy status to the caller, indicating they should call
                 * this function at a later point in time (or it may be called
                 * by an ISR).
                 */
                case IoStatus::Pending:
                    //last minute check in case someone cancelled this IOP
                    //during this loop but before this point. Saves the caller
                    //waiting and checking this IOP later only to find it was
                    //cancelled a while ago.
                    if (packet->status.Load(sl::Acquire) != IoStatus::Pending)
                        continue;
                    ClearBusyFlag();
                    return NpkStatus::Pending;

                /* Begin() was happy, move on to the next interface. If we
                 * overflow, someone messed up so set the status as an error
                 * and propagate it to the caller.
                 */
                case IoStatus::Continue:
                    packet->frameIndex++;
                    if (packet->frameIndex == packet->frameCount)
                    {
                        packet->frameIndex--;
                        packet->goingDown = false;
                        TrySetIopTerminalStatus(packet, IoStatus::Abort, 
                            { .code = (size_t)IoError::UnexpectedContinue });
                    }
                    continue;

                /* Begin() was able to complete it's operation, begin heading
                 * back up the stack calling End() functions on IoInterfaces,
                 * starting with the current frame.
                 */
                case IoStatus::Complete:
                    packet->goingDown = false;
                    continue;

                /* Begin() wasn't happy, a more specific reason should be set
                 * in the `abortCode` field of the IOP. This path is otherwise
                 * similar to `Complete` being returned.
                 */
                case IoStatus::Abort:
                    packet->goingDown = false;
                    TrySetIopTerminalStatus(packet, IoStatus::Abort, 
                        { .code = frame.abortCode });
                    continue;

                default:
                    packet->goingDown = false;
                    TrySetIopTerminalStatus(packet, IoStatus::Abort, 
                        { .code = (size_t)IoError::UnexpectedBeginStatus });
                    continue;
                }
            }
            else /* !packet->goingDown */
            {
                if (emitTraces)
                {
                    Log("IOP %p calling End() %u/%u: %p", LogLevel::Trace,
                        packet, packet->frameIndex + 1, packet->frameCount,
                        frame.interface->Begin);
                }

                //End() is optional, if it doesn't exist we'll assume the best
                //and say it returned `Complete`.
                auto status = IoStatus::Complete;
                if (frame.interface->End != nullptr)
                {
                    status = frame.interface->End(packet,
                        frame.interface->opaque, frame.stash);
                }
                if (emitTraces)
                {
                    Log("IOP %p End() call %u/%u returned %u (%s)",
                        LogLevel::Trace, packet, packet->frameIndex + 1,
                        packet->frameCount, status, 
                        IoStatusStr(status).Begin());
                }

                switch (status)
                {
                /* Same as pending for Begin() functions, interface needs more
                 * time.
                 */
                case IoStatus::Pending:
                    if (packet->status.Load(sl::Acquire) != IoStatus::Pending)
                        continue;
                    ClearBusyFlag();
                    return NpkStatus::Pending;

                /* End() wants a lower interface to do more work, so reverse
                 * direction and continue that way.
                 */
                case IoStatus::Continue:
                    if (packet->frameIndex == packet->frameCount - 1)
                    {
                        TrySetIopTerminalStatus(packet, IoStatus::Abort, 
                            { .code = (size_t)IoError::UnexpectedContinue });
                    }
                    else
                    {
                        packet->goingDown = true;
                        packet->frameIndex++;
                    }
                    continue;

                /* End() was happy, continue moving up the stack towards
                 * finishing the IOP.
                 */
                case IoStatus::Complete:
                    if (packet->frameIndex == 0)
                    {
                        TrySetIopTerminalStatus(packet, IoStatus::Complete, 
                            { .pointer = nullptr });
                        terminate = true;
                        break;
                    }
                    packet->frameIndex--;
                    continue;

                /* End() wasn't happy, set the abort status for this packet,
                 * then carry on back up the stack, allowing drivers to call
                 * their End() functions.
                 */
                case IoStatus::Abort:
                    TrySetIopTerminalStatus(packet, IoStatus::Abort,
                        { .code = frame.abortCode });
                    if (packet->frameIndex == 0)
                    {
                        terminate = true;
                        break;
                    }
                    packet->frameIndex--;
                    continue;

                default:
                    TrySetIopTerminalStatus(packet, IoStatus::Abort, 
                        { .code = (size_t)IoError::UnexpectedEndStatus });
                    terminate = true;
                    break;
                }
            }
        }

        auto status = packet->status.Load(sl::Acquire);
        while (status == IoStatus::Terminating)
        {
            sl::HintSpinloop();
            status = packet->status.Load(sl::Acquire);
        }

        size_t abortCode {};
        if (status == IoStatus::Abort)
            abortCode = packet->abortCode.Load(sl::Relaxed);

        if (completeCallback != nullptr)
            completeCallback(packet, completeCbArg);
        if (completeCondVar != nullptr)
            SignalWaitable(completeCondVar);

        ClearBusyFlag();
        if (status == IoStatus::Complete)
            return NpkStatus::Success;
        if (abortCode == static_cast<size_t>(IoError::Cancelled))
            return NpkStatus::Aborted;
        return NpkStatus::InternalError;
    }

    NpkStatus CancelIop(Iop* packet, IoError reason)
    {
        if (packet == nullptr)
            return NpkStatus::InvalidArg;

        if (TrySetIopTerminalStatus(packet, IoStatus::Abort, 
            { .code = static_cast<size_t>(reason) }))
            return NpkStatus::Success;

        return NpkStatus::NotAvailable;
    }

    NpkStatus RunIopUntilComplete(Iop* packet, bool poll)
    {
        return NpkStatus::Unsupported; (void)packet; (void)poll;
    }

    static NpkStatus ReadIopTerminalData(size_t* code, void** data, Iop* packet)
    {
        if (packet == nullptr)
            return NpkStatus::InvalidArg;

        IoStatus status;
        while (true)
        {
            status = packet->status.Load(sl::Acquire);

            if (status == IoStatus::Terminating)
                continue;
            if (status == IoStatus::Complete || status == IoStatus::Abort)
                break;
            return NpkStatus::NotAvailable;
        }

        if (status == IoStatus::Abort && code != nullptr)
            *code = packet->abortCode.Load(sl::Relaxed);
        else if (status == IoStatus::Complete && data != nullptr)
            *data = packet->completionData.Load(sl::Relaxed);

        return NpkStatus::Success;
    }

    NpkStatus ReadIopAbortCode(size_t* code, Iop* packet)
    {
        if (code == nullptr)
            return NpkStatus::InvalidArg;

        return ReadIopTerminalData(code, nullptr, packet);
    }

    NpkStatus ReadIopCompletionData(void** data, Iop* packet)
    {
        if (data == nullptr)
            return NpkStatus::InvalidArg;

        return ReadIopTerminalData(nullptr, data, packet);
    }

    sl::StringSpan IoStatusStr(IoStatus status)
    {
        switch (status)
        {
        case IoStatus::Initialized:
            return "initialized";
        case IoStatus::Pending:
            return "pending";
        case IoStatus::Continue:
            return "continue";
        case IoStatus::Complete:
            return "complete";
        case IoStatus::Abort:
            return "abort";
        case IoStatus::Terminating:
            return "terminating";
        default:
            return "<>";
        }
    }
}
