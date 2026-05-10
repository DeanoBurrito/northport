#pragma once

#include <Core.hpp>

namespace Npk
{
    struct VmSpace;
    struct Iop;
    struct Waitable;

    enum class IoError : size_t
    {
        /* An IoInterface returned IoStatus::Continue when it should not have.
         * This is usually the last IoInterface in the stack.
         */
        UnexpectedContinue,

        /* Operation was cancelled by user request.
         */
        Cancelled,

        /* An IoInterface's Begin() function did not return Continue,
         * Complete, Abort or Pending.
         */
        UnexpectedBeginStatus,

        /* An IoInterface's End() function did not return Continue,
         * Complete, Abort or Pending.
         */
        UnexpectedEndStatus,
    };

    enum class IoStatus : uint8_t
    {
        /*
         */
        Initialized,

        /*
         */
        Pending,

        /*
         */
        Continue,

        /*
         */
        Complete,

        /*
         */
        Abort,

        /* Transient state: only exists for correctness problems when
         * terminating an IOP. If this value is encountered in an IOP's status
         * the reader should spin until the value moves to `Abort` or 
         * `Complete`.
         */
        Terminating,
    };

    enum class IoType : uint16_t
    {
        /* Invalid operation type, an IOP of this type will always error.
         */
        Invalid = 0,

        /*
         */
        Open,

        /*
         */
        Close,

        /*
         */
        Read,

        /*
         */
        Write,

        /*
         */
        Lookup,

        /*
         */
        VendorBegin = 0x8000
    };

    enum class IopFlag
    {
        /* Internal use: indicates associated IO structures are in wired pool.
         */
        Wired,

        /* Internal use: indicates there is an active `ProgressIop()` call on
         * this IOP.
         */
        Busy,

        /* Indicates if trace messages should be emitted when processing this
         * IOP, useful for debugging the IO subsystem, IoInterfaces and their
         * providing drivers.
         */
        EmitTraces,
    };

    using IoFlags = sl::Flags<IopFlag>;

    enum class IoBufferType
    {
        /* Indicates the buffer holds no data, no `buffer.*` fields are active.
         */
        None,

        /* Indicates the buffer resides in kernel virtual memory, the
         * `buffer.kernel.*` fields are considered active.
         */
        KernelVirtual,

        /* Indicates the buffer resides in a user address space. The
         * `buffer.user.*` fields are considered active. Note that the user
         * address space may not be active while the IoBuffer is, so care
         * should be taken around direct buffer accesses.
         */
        UserVirtual,

        /* Indicates the buffer is a list of physical pages. The 
         * `buffer.physical.*` fields are considered active.
         * The buffer holds a reference to each PageInfo struct in `pages`.
         */
        PageList,
    };

    enum class IoBufferFlag
    {
        /* Internal use: indicates this buffer is allocated from the wired
         * pool.
         */
        WiredStruct,

        /* If the buffer is of type `PageList` this bit can be set to indicate
         * the data on all of the pages has been cleaned: the data held in the
         * pages is in main memory and visible to all devices in the system and
         * other cpus.
         * If this bit is cleared, no such assumptions can be made and the
         * recipient of the buffer should ensure the pages are cleaned if this
         * is required.
         */
        PagesClean,
    };

    using IoBufferFlags = sl::Flags<IoBufferFlag>;

    struct IoBuffer
    {
        /* Flags describing this buffer.
         */
        IoBufferFlags flags;

        /* Offset (in bytes) of the target data range from the beginning
         * of the specified buffer.
         */
        size_t offset;

        /* Length (in bytes) of the target data range from the beginning of
         * the specified buffer.
         */
        size_t length;

        /* Determines the meaning of the fields in the following union type.
         * Each enum member has a corresponding union sub-struct, except for
         * `None`, which means no fields are valid.
         */
        IoBufferType type;

        union
        {
            struct
            {
                uintptr_t base;
            } kernel;

            struct
            {
                uintptr_t base;
                VmSpace* space;
            } user;

            struct
            {
                sl::Span<PageInfo> pages;
            } physical;
        };
    };

    /*
     */
    struct IoInterface
    {
        /* I'm not documenting this field, you know what it is.
         */
        sl::RefCount refcount;

        /*
         */
        IoInterface* parent;

        /* Opaque pointer for use by the provider of this struct. Typically
         * a pointer to private (or) implementation details.
         */
        void* opaque;

        //`opaque` is per-IOI, `stash` is per-IOP.
        /*
         */
        IoStatus (*Begin)(Iop* iop, void* opaque, void** stash);

        /* Function to be called when completing an IOP and the dispatch
         * loop is walking back up the stack. This function should be used to
         * cleanup or reset any resources or state modified by this->Begin().
         *
         * The `opaque` argument is the same as passed to End(), which is the
         * `this->opaque` field of this IoInterface. The `stash` argument is
         * the same as `*stash` passed to Begin(). Stash is intended to hold
         * per-IOP data, and opaque is per-IoInterface.
         *
         * This field can be set to null, in which its return value is assumed
         * to be `Complete`, which tells the dispatch loop to continue up the
         * stack. If this function is present, the following return values
         * are allowed:
         * - Complete: end function completed successfully, this interface has
         *   completed its part in the IO operation.
         * - Pending: end function could not complete right away, needs more
         *   time. The dispatch loop exits and will try call this->End() again
         *   in the future.
         * - Continue: this function has more work for lower drivers to perform.
         *   This return value reverses the stack direction and the next loop
         *   cycle will call Begin() on the next lowest driver. If the lowest
         *   driver returns this value the IOP self aborts and will return an
         *   error.
         * - Abort: this function encountered an error and the IO operation
         *   will be at least partially incomplete. The dispatch loop will
         *   continue calling remaining end functions on remaining IOP frames,
         *   as a Begin() call on an IoInterface always has a matching End().
         *   To return detailed status information, the driver can set the
         *   `abortCode` field in its IopFrame struct.
         */
        IoStatus (*End)(Iop* iop, void* opaque, void* stash);
    };

    /* Parameter block: the active field here depends on the type of the
     * owning IOP. Consumers of the following fields are expected to fetch
     * the IOP type themselves.
     */
    union IopParams
    {
        struct
        {
        } open;

        struct
        {
        } close;

        struct
        {
        } readwrite;

        struct
        {
            sl::StringSpan name;
        } lookup;
    };

    struct IopFrame
    {
        void* stash;
        IoInterface* interface;
        IopParams params;
        size_t abortCode;
    };

    using IopCompletionCallback = void (*)(Iop* packet, void* opaque);

    struct Iop
    {
        IoType type;
        bool goingDown;
        uint8_t frameCount;
        uint8_t frameCapacity;
        uint8_t frameIndex;
        sl::Atomic<IoStatus> status;
        sl::Atomic<IoFlags> flags;
        union
        {
            sl::Atomic<void*> completionData;
            sl::Atomic<size_t> abortCode;
        };

        IopCompletionCallback completeCallback;
        void* callbackOpaque;
        Condition* completeCondition;

        sl::ListHook queueHook; //for usage by drivers

        sl::Span<IoBuffer> buffers;
        //TODO: child iops/parent linkage?
        IopFrame frames[];
    };

    using IopQueue = sl::List<Iop, &Iop::queueHook>;

    /* Attempts to allocate `count` number of IoBuffer structs. The `wired`
     * argument determines which pool the structs are allocated from: paged
     * (!wired) is preferred, wired should only be used if the IoBuffers are
     * involved in paging/swapping operations.
     * When finished with the IoBuffers, the caller should free their memory
     * via `FreeIoBuffers()`.
     * If successful, a pointer to the first allocated buffer is placed in
     * `*result`.
     */
    NpkStatus AllocIoBuffers(IoBuffer** result, size_t count, bool wired);

    /* Attempts to release memory used to store IoBuffers.
     */
    NpkStatus FreeIoBuffers(IoBuffer* buffers, size_t count);

    /* - management of buffers memory is caller's responsiblity.
     * - right now iop makes a copy of `*params`, is this safe enough?
     */
    NpkStatus CreateIop(Iop** result, IoInterface* target, IoType type, 
        IoFlags flags, IopParams* params, sl::Span<IoBuffer> buffers);
    
    /*
     */
    NpkStatus DestroyIop(Iop* packet);

    /*
     */
    NpkStatus StartIop(Iop* packet, bool waitUntilComplete);

    /* - returns NpkStatus::Pending if packet is pending.
     * - returns NpkStatus::Success if packet is complete (not necessarily
     *   completed in this call).
     * - returns NpkStatus::Aborted if packet was cancelled.
     * - returns NpkStatus::InternalError if en error occured.
     * - returns NpkStatus::Busy if the packet is already being progressed by
     *   someone else. This status usually indicates a bug in calling code.
     *
     * - this function should only be called from one context at a time, which
     *   must threaded and at passive ipl.
     * - if Begin() is called on an IOI, there is always a matching End() call,
     *   regardless of what Begin() returned.
     * - this function is the only way that completion processing happens for 
     *   an iop, it must be called.
     * - this function sets the busy flag when operating on an iop, including
     *   terminal state processing. The completion callback and condition var
     *   are signalled with the busy bit set, blocking any attempts to free
     *   the IOP until those operations are complete. Threads are allowed to
     *   spin on the status of an IOP, and since FreeIop() is synchronized
     *   with the busy bit, spinning threads are allowed to free the IOP.
     *   This is not recommended if a continuation or condition var are active
     *   for the IOP, since those may inspect IOP state, which may have already
     *   been freed by the spinning thread. Implementing this is left to
     *   callers of this API.
     *
     *   TODO: write this up properly
     */
    NpkStatus ProgressIop(Iop* packet);

    /*
     */
    NpkStatus CancelIop(Iop* packet, IoError reason);

    /* Runs an IOP until it has finished, in any sense. Depending on the value
     * of `poll` this function will either busy wait on the IOP status field or
     * wait on the IOP condition variable, if any. If `poll` is set and no
     * condition variable is allocated, this function will try to allocate one
     * and attach it. In that case this function will also free the allocated
     * memory before returning to the caller, in case the conditon variable
     * could not be allocated `NpkStatus::Shortage` will be returned.
     */
    NpkStatus RunIopUntilComplete(Iop* packet, bool poll);

    /* Attempts to read the abort code from `packet`. The abort code is only
     * available if the packet has finished the abort sequence, if this has
     * occured yet this function returns NotAvailable. If successfully able to
     * copy the abort code, it is placed into `*code` and Success is returned.
     */
    NpkStatus ReadIopAbortCode(size_t* code, Iop* packet);

    /* Attempts to copy the completion data pointer from `packet`. If the packet
     * completed successfully `*data` will contain the completion data and
     * this function returns Success. If the packet aborted or is in any other
     * state this function returns NotAvailable and `*data` is untouched.
     */
    NpkStatus ReadIopCompletionData(void** data, Iop* packet);

    /* Returns a string representation of `status`.
     */
    sl::StringSpan IoStatusStr(IoStatus status);
}
