#pragma once

#include <Types.hpp>
#include <Flags.hpp>
#include <Time.hpp>
#include <Span.hpp>
#include <Atomic.hpp>
#include <RefCount.hpp>
#include <containers/List.hpp>

namespace Npk
{
    /* Big picture stuff:
     * - IoInterface is the thing that can do io operations, an IOP/IO-Packet represents an operation.
     * - An IOP first runs down the stack of interfaces, until returns something other than 'continue'.
     *   - on 'error', reverse direction and call End() informing of the error, then return error to caller.
     *   - on 'pending', return pending to caller and wait until ProcessIop() is called again (likely by caller or from interrupt response code).
     *   - on 'shortage', same as 'error' response but not an error in itself.
     *   - on 'complete', reverse direction and call End(), return success status to caller - IO was performed.
     * - all IOP fields should be access by accessor functions within the kernel, the iop itself should be opaque.
     *   - HOWEVER! the size + alignment of the iop should be published so drivers can embed them in structs,
     *   or create them on the stack.
     * TODO: think about the flow when removing an IoInterface, and also cancelling iops
     */

    struct Iop;
    struct Waitable;

    enum class IoStatus : uint8_t
    {
        Invalid = 0, //placeholder
        Error, //fatal error, set direction to ending and cleanup. Return error to caller.
        Pending, //hardware is doing things, check back later
        Continue, //Begin()/End() is happy, carry on in the same direction. If the final End() call, we're done.
        Complete, //Begin() completed the IO op, reverse direction and call End() functions.
        Shortage, //retryable error, temporarily ran out of resources.
        Timeout, //as it says
    };

    enum class IoType : uint16_t
    {
        Invalid = 0,
        Open,
        Close,
        Read,
        Write,

        VendorBegin = 1ul << 15
    };

    enum class IoFlag
    {
    };

    using IoFlags = sl::Flags<IoFlag, uint16_t>;

    struct IoInterface
    {
        IoInterface* parent; //target-device chain 'next' pointer
        void* opaque;

        //opaque is the per-IoInterface pointer, stash is the per-iop version
        IoStatus (*Begin)(Iop* iop, void* opaque, void** stash);
        IoStatus (*End)(Iop* iop, void* opaque, void* stash); //NOTE: optional, success assumed if nullptr
    };

    struct IopFrame
    {
        void* stash;
        IoInterface* interface;
    };

    struct Iop
    {
        IoType type;
        IoFlags flags;
        bool directionDown; //whether going up/down stack
        bool started;
        bool complete;
        uint8_t addressSize;
        uint8_t frameCount;
        uint8_t frameCapacity;
        uint8_t frameIndex;
        sl::Atomic<IoStatus> status;
        void* targetAddress;
        sl::ListHook queueHook; //for usage by drivers, or kernel work items once the iop is complete

        void (*Continuation)(Iop* iop, void* opaque); //function to run after completing iop, run at passive ipl
        void* continuationOpaque;
        Waitable* onComplete; //cond var, can be null

        //TODO: buffer specifying data location
        //TODO: child iops/parent linkage?
        IopFrame frames[];
    };

    using IopQueue = sl::List<Iop, &Iop::queueHook>;

    bool ResetIop(Iop* packet);
    bool PrepareIop(Iop* packet, IoType type, IoInterface* target, 
        void* address, size_t addressSize, IoFlags flags);
    void SetIopContinuation(Iop* packet, void (*cont)(Iop*, void*), void* opaq);
    void SetIopCondVar(Iop* packet, Waitable* condVar);
    IoStatus GetIopStatus(Iop* packet);
    Waitable* GetIopCondVar(Iop* packet);

    IoStatus StartIop(Iop* packet);
    IoStatus CancelIop(Iop* packet);
    IoStatus ProgressIop(Iop* packet);
    IoStatus PollUntilComplete(Iop* packet, sl::TimeCount timeout);
    IoStatus WaitUntilComplete(Iop* packet, sl::TimeCount timeout);

    sl::StringSpan IoStatusStr(IoStatus status);
}
