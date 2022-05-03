# System Call Details
This is the official spec for northport system calls, see the other document on implementation details. 
System calls are grouped into ranges of related functions.

System calls return a status code in the `id` register. `0` and `0x404` are special values meaning success and system call not found respectively. All other values differ in meaning by the type of system call, and are documented below in their own sections.

Any magic numbers, structures and functions referenced are codified in the following files:
- for magic numbers, see [libs/np-syscall/include/SyscallEnums.h](../../libs/np-syscall/include/SyscallEnums.h)
- for structs, see [libs/np-syscall/include/SyscallStructs.h](../../libs/np-syscall/include/SyscallStructs.h)
- for functions, see [libs/np-syscall/include/SyscallFunctions.h](../../libs/np-syscall/include/SyscallFunctions.h)


----
# 0x0* - Experiments
The first 16 syscalls are reserved for testing, and should not be considered stable *at all*, and are reserved for development purposes.

## 0x0 - LoopbackTest
Does nothing and returns immediately, used for testing syscalls work at all.

### Args:
- None.

### Returns:
- None.

### Notes:
- Always succeeds. Used to test I havent broken syscalls when I update related code.


# 0x1* - Mapping Memory and Files
The following functions relate to mapping/unmapping memory regions for a given program. These can only affect other regions of the same program, previously requested by the program. Regions allocated by the system (shared buffer, and the program binary itself) are protected from changes in this manner.
Userspace programs can only map memory in the lower half.

A number of these functions take a bitfield of memory flags as an argument, which are defined as the following:
- `bit 0`: write enable, otherwise memory will be read-only.
- `bit 1`: execute enable. Set to allow instruction fetches from this memory.
- `bit 2`: user-visible. This is forced on for user processes, but this may have other applications for kernel level programs (drivers).
- `bits 3 and 4`: reserved, ignored if set.

## 0x10 - MapMemory
Ensures a region of memory is mapped with a specific set of flags.

### Args:
- `arg0`: address to use as base of mapping. This will be aligned down to next native page. 
- `arg1`: number of bytes to map. This will be aligned up to the next native page.
- `arg2`: flags for the mapped region. Bit 0 is write-enable, bit 1 is execute-enable, bit 2 is user-accessible (forced on for user programs).
- `arg3`: ignored.

### Returns:
- `arg0`: base address of mapping that occured. Zero if mapping failed.
- `arg1`: number of bytes actually mapped. Zero if mapping previously existed.
- all other return values should be ignored.

### Notes:
- If the area mapped by this function overlaps with another, it will not modify the flags of the existing area's memory. This is by design: if you intentionally overlap regions and want these flags applied, do it yourself using ModifyMemoryFlags, after mapping both areas. Alternatively, don't overlap mapped regions.

## 0x11 - UnmapMemory
Unmaps a region of memory.

### Args:
- `arg0`: address for base of unmap, this is rounded down to the nearest page.
- `arg1`: number of bytes to unmap, this is rounded up to the nearest native page. This is capped at the length of the originally mapped region.
- all other args ignored.

### Returns:
- `arg0`: if region completely unmapped, 1 or 2 if memory was splintered on the left or right sides, and 3 if splintered on both sides.
- all other return values should be ignored.

### Notes:
- The bitmap returned in `arg0` can be thought of as the low bit being set if a mapped memory was left on the low side of the unmapped regioin, and the opposite is true.

## 0x12 - ModifyMemoryFlags
Modifies the flags of a region of previously mapped memory.

### Args
- `arg0`: base address of modified region. This is rounded down to the nearest native page.
- `arg1`: number of bytes to modify, this will round up to the nearest native page.
- `arg2`: memory flags to apply to new region.
- all other args ignored.

### Returns:
- `arg0`: base address of modified region.
- `arg1`: number of bytes modified.

### Notes:
- This will affect any regions or parts of inside of the specified region. Any memory inside the specified area that is not already mapped will be ignored.
- Processes cannot modify flags for regions marked as `system`, only the kernel can.


----
# 0x2* - Device Management
Functions for iterating through devices, and setting up ways to communicate further with them. A number of these functions will take a device type, which is defined as follows:
- `0`: Graphics adaptor.
- `1`: Graphics framebuffer.
- `2`: Keyboard.
- `3`: Mouse.

Device functions can also return the following errors in the `id` register:
- `1`: The requested feature is not available.
- `2`: No primary device exists for this type.
- `3`: The specified device type is unknown, cannot return any data about it.

Device info will be returned as a 32-byte structure across the syscall registers, where `arg0` is the low bytes and `arg3` is the high bytes. 
The layouts of these structs are defined below.

### Device type 0, Graphics Adaptor:
- `u64`: device id

### Device type 1, Graphics Framebuffer:
- `u64`: device id
- `u16`: width
- `u16`: height
- `u16`: stride (bytes per scanline)
- `u16`: bits per pixel
- `u64`: framebuffer base address
- `u8`: red offset
- `u8`: green offset
- `u8`: blue offset
- `u8`: reserved
- `u8`: red bits mask
- `u8`: green bits mask
- `u8`: blue bits mask
- `u8`: reserved.

### Device type 2, Keyboard:
- `u64`: device id
- `u64`: aggregate keyboard device id

### Device type 3, Mouse:
- `u64`: device id
- `u64`: aggregate mouse device id
- `u16`: axis count
- `u16`: button count

## 0x20 - GetPrimaryDeviceInfo
Returns the device id and some basic (device-dependent) info about the primary device for a category.

### Args:
- `arg0`: The device type to query (see above).
- `arg1`: Set to a non-zero value to get more detailed info about a device. 
- All other args are ignored.

### Returns:
The return values depend on the device type, and whether basic or advanced info was queried. See their description above.

### Notes:
- The idea of there being a single primary device for some device types shouldn't be taken too seriously. For example if there are multiple monitors connected, the primary framebuffer will always be the main monitor. Application developers using a framebuffer directly will want to iterate through
the available choices, and pick the most appropriate one in that case. Primary devices are just a hint.

## 0x21 - GetDevicesOfType
//TODO:

## 0x22 - GetDeviceInfo
//TODO:

## 0x23 - EnableDeviceEvents
Tells a device to start sending events to this process. The events received are device specific, for example a keyboard will send key press events, a mouse will send mouse movements, an audio device might send requests for more buffer data.

### Args:
- `arg0`: Id of the device to receive events from.
- All other args are ignored.

### Returns:
- Nothing.

### Notes:
- Some device types (keyboard and mouse) will forward their inputs into an aggregate device. The id of this device is usually avaiable in their device info structures. Unless a program only wants to receive events from a specific device, they should enable/disable events from the aggregate device instead.

## 0x24 - DisableDeviceEvents
Disables receiving events from this device.

### Args:
- `arg0`: Id of the device to stop receiving events from.
- All other args are ignored.

### Returns:
- Nothing.

### Notes:
- None.

----
# 0x3* - Filesystem Operations
The filesystem uses a single root style (unix, rather than NT), and URL-style protocols are not currently supported (and will return an error).

Filesystem functions can return the following errors in the `id` register:
- `1`: No file or directory was found with that name.
- `2`: No resource id. The operation could not be completed as the resource could not be attached to this process.
- `3`: Invalid buffer range. A buffer access of some kind (including strings of filenames) failed.

## 0x30 - GetFileInfo
Checks if a file exists, and if it does returning information about the file. Things like file size, attributes/permissions, owner.

### Args:
- `arg0`: pointer to a c-style string containing the file path. This path must be absolute.
- all other args are ignored.

### Returns:
- `arg3`: pointer to a `FileInfo` struct.
- all other return values should be ignored.

### Notes:
- This function can be used to test if a file exists, and also to get basic info about a file.

## 0x31 - OpenFile
Attempts to open a file at the given path, returns a non-zero file descriptor if successful.

### Args:
- `arg0`: pointer to a c-style string containing the file path. This path must be absolute.
- all other args are ignored.

### Returns:
- `arg0`: file handle. If syscall was successful, this unsigned integer can be used as handle for other file operations. This is process-local, but shared between threads.
- all other return values should be ignored.

### Notes:
- None.

## 0x32 - CloseFile
Closes (and disposes of) an existing file handle.

### Args:
- `arg0`: file handle. An unsigned integer returned from OpenFile() or similar sources.
- all other args are ignored.

### Returns:
- Nothing.

### Notes:
- The kernel may re-assign this handle id to something else in the future, and it is up to programmer to ensure they do not accidentally retain the handle anywhere after calling this.

## 0x33 - ReadFromFile
Reads a number of bytes from an open file handle, into a user-supplied buffer.

### Args:
- `arg0`: file handle, returned from OpenFile or other sources.
- `arg1`: bits 31:0 are the file offset to start reading from, bits 63:32 are the buffer offset to read into.
- `arg2`: pointer to a byte buffer to read into (user controlled).
- `arg3`: length of data to read.

### Returns:
- `arg0`: number of bytes read.
- all other return values should be ignored.

### Notes:
- All offsets and lengths are in bytes.

## 0x34 - WriteToFile
Writes bytes from a user buffer to a file handle.

### Args:
- `arg0`: file handle, returned from OpenFile or other sources.
- `arg1`: bits 31:0 are the file offset to start writing to, bits 63:32 are the buffer offset to read from.
- `arg2`: pointer to a byte buffer to read from.
- `arg3`: length of data to write.

### Returns:
- `arg0`: number of bytes written.
- all other return values should be ignored.

### Notes:
- All offsets and lengths are in bytes.

## 0x35 - FlushFile


----
# 0x4* - Inter-Process Communication
Two flavours of IPC are supported: discrete messages (or packets, mailbox style) and continuous streams (memory buffers style). Access to IPC is described using a 4bit integer, occupying bits 63:60 of the flags argument. The access list for an ipc endpoint can be modified using the `ModifyIpcConfig` system call.
Ids can be either thread or process ids, with a process id allowing all it's threads the same access.
They accept the following values:
- `0`: No read/write allowed at all. Effectively disables operation on the IPC endpoint.
- `1`: Public. Any process can read or write to this endpoint.
- `2`: Selected only. Only pre-approved process ids can access this endpoint. This list of ids starts empty and can be modified by the ModifyIpcConfig syscall.
- `3`: Private. This forces 1-to-1 communication, only a single remote process can access this endpoint.

Some IPC functions will take a flags argument, which is a bitfield defined as:
- `bit 0`: Use shared physical memory. Also known as 'zero copy' as both processes are directly writing to the same underlying memory, the kernel is never involved.
- `bits 60 to 63 (inclusive)`: reserved for the access modifiers (see above).

For stream-based IPC, the server can start or stop a listener, while a number of clients can open or close connections to said server. Sending data is done using the read/write ipc syscalls. 
Streams are named, but as IPC subsystem is separate to the vfs there are no conflicts. This also means you cannot access IPC streams from the filesystem.

For message-based IPC, the server creates a named mailbox and clients can use PostToMailbox to send messages. The server can then destroy the mailbox when it is done. Posting to an non-existent mailbox returns an error.
When mail is sent to the mailbox's process, an event with type `IncomingMail` is posted into the process's event queue.

IPC functions can return the following errors:
- `1`: Could not start the IPC stream.
- `2`: Unable to attach the stream as a process resource. Stream was closed automatically.
- `3`: A buffer operation (including reading the stream name string) failed.
- `4`: Mail could not be delivered.

## 0x40 - StartIpcStream
Opens an IPC stream with the requested permissions. This stream is always read/write.

### Args:
- `arg0`: pointer to a c-string with the requested stream name.
- `arg1`: bitfield of requested flags. See the description in SyscallEnums.h.
- `arg2`: minimum size of stream buffer.
- all other args are ignored.

### Returns:
- `arg0`: if the syscall succeed, contains the handle id of the syscall stream. 
- `arg1`: actual size of stream buffer.
- `arg2`: if using shared memory, address of the stream buffer in this process. Otherwise unused.
- all other return values should be ignored.

### Notes:
- The stream buffer size may be bigger than requested depending on the implementation, and is usually just rounded up to the nearest page size.
- If a stream is created with `UseSharedMemory` flag, the used memory can be accessed directly, no need to use read/write ipc syscalls.

## 0x41 - StopIpcStream
Closes an existing IPC stream. 

### Args:
- `arg0`: handle of the IPC stream to stop.
- all other args are ignored.

### Returns:
- Nothing.

### Notes:
- Any open handles to this stream are considered invalid (closing them will be ignored, and reading/writing to it will result in errors.
- There is no notification that the stream is closing, the handles are simply invalidated internally by the kernel. If this event is important to you, it's left to the user to implement as part of your own protocol.

## 0x42 - OpenIpcStream
Connects to an existing IPC stream, making it available for access.

### Args:
- `arg0`: pointer to c-string with the requested stream name.
- `arg1`: bitfield of requested flags, similar to StartIpcStream.
- all other args are ignored.

### Returns:
- `arg0`: the resource handle of the IPC stream that was successfully opened.
- `arg1`: size of stream buffer.
- `arg2`: if using shared memory, address of buffer in this process. Otherwise unused.
- all other return values should be ignored.

### Notes:
- None.

## 0x43 - CloseIpcStream
Closes an open stream handle. This does not stop the stream, it only detaches it from the current process.

### Args:
- `arg0`: handle of the IPC stream to close.
- all other args are ignored.

### Returns:
- Nothing.

### Notes:
- There is not signalling of a process leaving a stream, this is up to the user to implement as part of your protocol if you care.

## 0x44 - ReadFromIpcStream

## 0x45 - WriteToIpcStream

## 0x46 - CreateMailbox
Creates an IPC mailbox with the requested flags. For each mail that is received the process will be notified by an `IncomingMail` event.

### Args:
- `arg0`: pointer to a c-string with the requested mailbox name.
- `arg1`: bitfield of requested flags.
- all other args ignored.

### Returns:
- `arg0`: handle of the IPC mailbox, for config purposes.
- all other return values should be ignored.

### Notes:
- All ipc endpoints share the same namespace, meaning a mailbox and stream cannot have the same name.
- Currently mailboxes are implemented *on top* of an IPC stream, meaning that the usual IPC config operations can be performed with the returned handle. However, internally mailboxes use several structures inside of the shared buffer, so while it can be accessed directly, it is ill-advised. 

## 0x47 - DestroyMailbox
Destroys an existing mailbox, any unread or currently sending mail will be discarded. 

### Args:
- `arg0`: mailbox handle, returned from CreateMailbox().
- all other args ignored.

### Returns:
- Nothing.

### Notes:
- None.

## 0x48 - PostToMailbox
Sends mail to a remote mailbox. Will return an error if mail could not be delivered for any reason.

### Args:
- `arg0`: pointer to a c-string containing the mailbox name.
- `arg1`: base address of the mail to send.
- `arg2`: length in bytes of mail to send.
- all other args ignored.

### Returns:
- Nothing.

### Notes:
- None.

## 0x49 - ModifyIpcConfig
Configures an existing IPC stream or mailbox. Args 2 and 3 change their function depending on arg0, which determines the operation to perform. Arg1 is the ipc endpoint (stream or mailbox) to operate on. The currently supported operations (`arg0`) are listed below:

- `0`: reserved.
- `1`: add an ID to the ipc access list. `arg2` is the id.
- `2`: remove an ID from the ipc access list. `arg2` is the id.
- `3`: change the ipc access flags. `arg2` is the new flags (in bits 60:63)
- `4`: transfer ownership. `arg2` is the process id of the new owner. 

### Returns:
- Nothing.

### Notes:
- Only the owner of the endpoint can use this system call. This id has a process-level granuality (i.e. any thread in the owner process is granted owner-level access).


----
# 0x5* - General Utilities
This group of system calls is a collection of unrelated utilties.

## 0x50 - Log
Writes a log entry to the global log.

### Args:
- `arg0`: pointer to a c-string containing the text to be logged.
- `arg1`: level of log to be written: 0 = info, 1 = warning, 2 = error, 4 = verbose.
- all other args ignored.

### Returns:
- Nothing.

### Notes:
- Unlike the kernel function `logf()` this syscall does not provide any formatting support for security reasons. It writes the input string to the currently active log backends verbatim.
- The logging level values mirror those in the kernel logging system, with the exception of the `fatal` level not being available. Any attempts to issue a fatal log result in the log being emitted as an error instead.


----
# 0x6* - Program Events
A process can have events sent to it over it's lifetime. These share a common header, and can optionally describe a buffer of attached data.

The common header is 2x unsigned 32-bit integers (first indicating the type of the event - see below, second indicating the length of the optional data section), followed by an unsigned 64-bit integer containing the address of the data. If the length field is zero, there is no data section and this address field is undefined.

Commonly these values are compressed into 2 64-bit ints, with the event type (as the lower 32-bits), and length (as the upper 32-bits) being compacted into a single value.

The most significant bit of the event type is a flag indicating whether the event type is application defined (if set), or a built in event type (if cleared). 

Built in event types:
- `0, Null`: If this is received an error has occured, and this event should be discarded.
- `1, ExitGracefully`: The process is being requested to close and exit on it's own.
- `2, ExitImmediately`: A process will never actually process this event, it'll kill the process upon being received.
- `3, IncomingMail`: A process has received IPC mail.
- `4, KeyEvent`: A keyboard event, forwarded from the aggregate keyboad device (all keyboards forward their inputs here).
- `5, MouseEvent`: A mouse event, forwarded from the aggregate mouse device (all other mice forward their inputs here).

## 0x60 - PeekNextEvent
Returns the header of the next pending event, without consuming it.

### Args:
- All args ignored.

### Returns:
- `arg0`: bits 31:0 are the event type, bits 63:32 are the data length.
- All other return values should be ignored.

### Notes:
- None.

## 0x61 - ConsumeNextEvent
Consumes the next event, and if given a non-null buffer will copy the data into it before removing it on the kernel side.

### Args: 
- `arg0`: buffer to write event data into
- All other args are ignored.

### Returns:
- `arg0`: bits 31:0 are the event type, bits 63:32 are the data length.
- All other return values should be ignored.

### Notes:
- None.

## 0x62 - GetPendingEventCount
Returns the number of unprocessed events.

### Args:
- None.

### Returns:
- `arg0`: Number of pending events for this process.
- All other return values should be ignored.

### Notes:
- Not sure why I implemented this, not much you can do with it.
