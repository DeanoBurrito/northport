#pragma once

#include <Types.hpp>
#include <Flags.hpp>
#include <Time.hpp>
#include <Span.hpp>
#include <Atomic.hpp>
#include <containers/List.hpp>

namespace Npk
{
    // Big picture stuff:
    // - IOPs should be opaque outside of the io subsystem, but their size + alignmentshould be published.
    // - Think about how removing an IoInterface will interact with IOPs.
    struct Iop;
    struct Waitable;

    enum class IoStatus : uint8_t
    {
        Invalid,
        Error,
        Pending,
        Continue,
        Complete,
        Shortage,
        Timeout,
    };

    enum class IoType : uint16_t
    {
        Invalid,
        Open,
        Close,
        Read,
        Write,

        VendorBegin = 0x8000
    };

    enum class IoFlag
    {
    };

    using IoFlags = sl::Flags<IoFlag, uint16_t>;

    struct IoInterface
    {
        IoInterface* parent;
        void* opaque;

        //`opaque` is per-IOI, `stash` is per-IOP.
        IoStatus (*Begin)(Iop* iop, void* opaque, void** stash);
        //NOTE: `End()` is optional, assumed to return IoStatus::Complete if nul
        IoStatus (*End)(Iop* iop, void* opaque, void* stash);
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
        bool directionDown;
        bool started;
        bool complete;
        uint8_t addressSize;
        uint8_t frameCount;
        uint8_t frameCapacity;
        uint8_t frameIndex;
        sl::Atomic<IoStatus> status;
        void* targetAddress;
        sl::ListHook queueHook; //for usage by drivers

        void (*Continuation)(Iop* iop, void* opaque);
        void* continuationOpaque;
        Waitable* onComplete;

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
    IoStatus PollUntilComplete(Iop* packet, sl::TimeCount timeout, 
        sl::StringSpan reason = {});
    IoStatus WaitUntilComplete(Iop* packet, sl::TimeCount timeout,
        sl::StringSpan reason = {});
}
