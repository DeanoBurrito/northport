#include <IoPrivate.hpp>
#include <Core.hpp>

/* I/O Packet (IOP) thoughts and theory:
 *
 * First things first, a lot of the IOP stuff is based on prior art by other
 * kernels: 
 * - Keyronex: https://github.com/Keyronex/Keyronex
 * - Minoca: https://github.com/minoca/os
 * No code was directly taken from those projects so I've left out the license,
 * but I thought it important to mention them.
 *
 * IOPs are the beating heart of the io subsystem, each IOP represents a single
 * operation and holds any state necessary to suspend and resume it. The obvious
 * use for them is dealing with slow hardware, and chaining multiple operations
 * together. They are also a good primitive for general async operations 
 * throughout the kernel, which we *can* already do via c++ coroutines, but 
 * thats not available across kernel/driver boundaries.
 *
 * Each operation requires a target, in northport we call these IoInterfaces
 * (IOIs). IOIs can also have a parent IOI, useful for representing a stack
 * of drivers (e.g. usb keyboard -> usb hid -> usb -> pci, in this case the
 * usb keyboard is the initial target IOI).
 *
 * When running an IOP, each IOI has its `Begin()` function called. The result
 * here determines if the IOP pauses and returns to the caller now, carries on
 * to the next IOI in the stack. `Begin()` can also indicate the IOP is
 * complete and the IOP will reverse direction, calling each IOIs `End()`
 * function (if present), allowing IOIs to cleanup or request further
 * action from target IOIs.
 */
namespace Npk
{
    bool ResetIop(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, false);
        NPK_CHECK(packet->complete, false);

        packet->type = IoType::Invalid;
        packet->flags.Reset();
        packet->directionDown = true;
        packet->started = false;
        packet->complete = false;
        packet->addressSize = 0;
        packet->frameCount = 0;
        //leave `frameCapacity` alone, since it tells us the size of the memory
        //block in use by this IOP.
        packet->frameIndex = 0;
        packet->status = IoStatus::Invalid;
        packet->targetAddress = nullptr;
        packet->Continuation = nullptr;
        packet->continuationOpaque = nullptr;
        packet->onComplete = nullptr;

        return true;
    }

    bool PrepareIop(Iop* packet, IoType type, IoInterface* target, 
        void* address, size_t addressSize, IoFlags flags)
    {
        NPK_CHECK(packet != nullptr, false);
        NPK_CHECK(target != nullptr, false);

        //basically just checks to ensure that `ResetIop()` was called earlier
        NPK_CHECK(!packet->started, false);
        NPK_CHECK(!packet->complete, false);
        NPK_CHECK(packet->type == IoType::Invalid, false);
        NPK_CHECK(packet->status == IoStatus::Invalid, false);

        packet->type = type;
        packet->flags = flags;
        packet->addressSize = addressSize;
        packet->targetAddress = address;

        packet->frameCount = 0;
        for (size_t i = 0; i < packet->frameCapacity; i++)
        {
            auto& frame = packet->frames[i];

            if (target == nullptr)
                break;

            frame.interface = target;
            target = target->parent;
            packet->frameCount++;
        }

        //ensure we reached the end of the IoInterface chain, if target still
        //has a value, we ran out of frames and therefore the iop is invalid.
        NPK_CHECK(target == nullptr, false);

        return true;
    }

    void SetIopContinuation(Iop* packet, void (*cont)(Iop*, void*), void* opaq)
    {
        //NOTE: it's called 'opaq' instead of 'opaque' because I didnt want to
        //start a new line for the function declaration, and 'opaq' works.
        NPK_CHECK(packet != nullptr, );
        NPK_CHECK(!packet->started, );
        NPK_CHECK(!packet->complete, );

        Log("Setting IOP %p continuation to %p (arg=%p)", LogLevel::Trace,
            packet, cont, opaq);

        packet->Continuation = cont;
        packet->continuationOpaque = opaq;
    }

    void SetIopCondVar(Iop* packet, Waitable* condVar)
    {
        NPK_CHECK(packet != nullptr, );
        NPK_CHECK(!packet->started, );
        NPK_CHECK(!packet->complete, );

        Log("Setting IOP %p condition variable to %p", LogLevel::Trace,
            packet, condVar);

        packet->onComplete = condVar;
    }

    IoStatus GetIopStatus(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);
        NPK_CHECK(packet->complete, IoStatus::Invalid);

        return packet->status.Load(sl::Relaxed);
    }

    Waitable* GetIopCondVar(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, nullptr);

        return packet->onComplete;
    }

    IoStatus StartIop(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);
        NPK_CHECK(!packet->started, IoStatus::Invalid);
        NPK_CHECK(!packet->complete, IoStatus::Invalid);

        packet->started = true;
        return ProgressIop(packet);
    }

    IoStatus CancelIop(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);

        NPK_UNREACHABLE();
        //TODO: 
    }

    IoStatus ProgressIop(Iop* packet)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);
        NPK_CHECK(packet->started, IoStatus::Invalid);
        NPK_CHECK(!packet->complete, packet->status);

        while (true)
        {
            NPK_ASSERT(packet->frameIndex <= packet->frameCount);

            auto frame = packet->frames[packet->frameIndex];
            auto iface = frame.interface;
            if (packet->directionDown)
            {
                frame.stash = nullptr;
                auto status = iface->Begin(packet, iface->opaque, &frame.stash);

                switch (status)
                {
                /* The device needs more time, leave the IOP as-is and return
                 * the pending status to the caller. When someone tries to
                 * progress this IOP again, we'll call the same interface
                 * Begin() routine, which checks if hardware has completed the 
                 * op at that point in time.
                 */
                case IoStatus::Pending:
                    return IoStatus::Pending;

                /* Begin() was happy, move onto the next interface in the stack.
                 */
                case IoStatus::Continue:
                    packet->frameIndex++;
                    if (packet->frameIndex == packet->frameCount)
                    {
                        Log("IOP %p failed to complete at final level (%u)",
                            LogLevel::Error, packet, packet->frameIndex - 1);
                        packet->frameIndex--;
                        packet->directionDown = false;
                        packet->status = IoStatus::Error;
                    }
                    continue;

                /* Begin() was able to complete the operation, start heading
                 * back up the stack (starting with the current frame) and 
                 * calling End() functions.
                 */
                case IoStatus::Complete:
                    packet->directionDown = false;
                    continue;

                /* Begin() couldnt allocate required resources, we abort the op
                 * and return the status to the caller.
                 */
                case IoStatus::Shortage:
                    packet->status = IoStatus::Shortage;
                    packet->directionDown = false;
                    Log("IOP %p is aborting due to resource shortage at level"
                        " %u", LogLevel::Trace, packet, packet->frameIndex);
                    continue;

                /* Begin() ran into an error and refused to continue, abort the
                 * op and return to the caller.
                 * We treat any unknown status results as an error too.
                 */
                default:
                case IoStatus::Error: 
                    packet->status = IoStatus::Error;
                    packet->directionDown = false;
                    Log("IOP %p is aborting due to error at level %u.", 
                        LogLevel::Trace, packet, packet->frameIndex);
                    continue;
                }
            }
            else /* !packet->directionDown */
            {
                //End() is optional, if it doesnt exist assume the best result.
                auto status = IoStatus::Complete;
                if (iface->End != nullptr)
                    status = iface->End(packet, iface->opaque, frame.stash);

                switch (status)
                {
                /* Same as Pending for Begin() calls, device needs more time.
                 */
                case IoStatus::Pending:
                    return IoStatus::Pending;

                /* End() wants to do more work, start heading down the stack
                 * again.
                 */
                case IoStatus::Continue:
                    packet->directionDown = true;
                    packet->frameIndex++;
                    continue;

                /* End() was happy, continue moving up the stack towards
                 * finishing the IOP.
                 */
                case IoStatus::Complete:
                    if (packet->frameIndex == 0)
                    {
                        if (packet->status == IoStatus::Invalid)
                            packet->status = IoStatus::Complete;
                        packet->complete = true;

                        if (packet->onComplete != nullptr)
                            SignalWaitable(packet->onComplete);
                        if (packet->Continuation != nullptr)
                            Private::QueueContinuation(packet);
                        break;
                    }
                    else
                    {
                        packet->frameIndex--;
                        continue;
                    }

                /* End() ran into an error, we're already heading back up the
                 * stack so we carry on. We dont return the error status to
                 * the caller directly since the operation is assumed to have
                 * completed successfully (otherwise Begin() would have
                 * errored).
                 */
                default:
                case IoStatus::Error:
                    Log("IOP %p level %u encountered an error during End().",
                        LogLevel::Error, packet, packet->frameIndex);
                    continue;
                }
            }
        }
    }

    static IplSpinLock<Ipl::Dpc> pollingEntriesLock;
    static sl::List<IoPollingEntry, &IoPollingEntry::hook> pollingEntries;

    size_t GetIoPollEntries(size_t offset, sl::Span<IoPollingEntry> entries)
    {
        size_t head = 0;

        sl::ScopedLock scopeLock(pollingEntriesLock);
        for (auto it = pollingEntries.Begin(); it != pollingEntries.End(); ++it)
        {
            while (offset > 0)
            {
                offset--;
                continue;
            }

            if (head == entries.Size())
                break;

            //TODO: if 'reason' isnt statically allocated we'll have bugs!!
            entries[head++] = *it;
            entries[head - 1].hook = {};
        }

        return head;
    }

    IoStatus PollUntilComplete(Iop* packet, sl::TimeCount timeout, 
        sl::StringSpan reason)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);
        NPK_CHECK(!packet->complete, packet->status);
        NPK_CHECK(timeout.ticks != 0, IoStatus::Invalid);

        const auto now = GetMonotonicTime();
        const auto end = timeout.Rebase(now.Frequency).ticks + now.epoch;

        IoPollingEntry entry {};
        entry.packet = packet;
        entry.callsite = reinterpret_cast<uintptr_t>(SL_RETURN_ADDR);
        entry.expiry = end;
        entry.reason = reason;

        Log("%zu is polling for iop to complete: %p, reason=%.*s", 
            LogLevel::Verbose, entry.callsite, entry.packet, 
            (int)entry.reason.Size(), entry.reason.Begin());

        pollingEntriesLock.Lock();
        pollingEntries.PushBack(&entry);
        pollingEntriesLock.Unlock();

        while (GetMonotonicTime().epoch < end && !packet->complete)
            sl::HintSpinloop();

        pollingEntriesLock.Lock();
        pollingEntries.Remove(&entry);
        pollingEntriesLock.Unlock();

        if (GetMonotonicTime().epoch >= end)
            return IoStatus::Timeout;

        return GetIopStatus(packet);
    }

    IoStatus WaitUntilComplete(Iop* packet, sl::TimeCount timeout, 
        sl::StringSpan reason)
    {
        NPK_CHECK(packet != nullptr, IoStatus::Invalid);
        NPK_CHECK(packet->onComplete != nullptr, IoStatus::Invalid);
        NPK_CHECK(!packet->complete, packet->status);

        WaitEntry waitEntry {};
        auto status = WaitOne(packet->onComplete, &waitEntry, timeout, reason);
        if (status != WaitStatus::Success)
            return IoStatus::Invalid;

        return GetIopStatus(packet);
    }
}
