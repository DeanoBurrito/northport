# System Call Details
This is the official spec for northport system calls, see the other document on implementation details. 
System calls are grouped into ranges of related functions.

Any magic numbers, structures and functions referenced are codified in the following files:
- for magic numbers, see [libs/np-syscall/include/SyscallEnums.h](../../libs/np-syscall/include/SyscallEnums.h)
- for structs, see [libs/np-syscall/include/SyscallStructs.h](../../libs/np-syscall/include/SyscallStructs.h)
- for functions, see [libs/np-syscall/include/SyscallFunctions.h](../../libs/np-syscall/include/SyscallFunctions.h)


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
- `arg0`:`0 if region completely unmapped, 1 or 2 if memory was splintered on the left or right sides, and 3 if splintered on both sides.
- all other return values should be ignored.

### Notes:
- The bitmap returned in `arg0` can be thought of as the low bit being set if a mapped memory was left on the low side of the unmapped regioin, and the opposite is true.

## 0x12 - ModifyMemoryFlags
Modifies the flags of a region of previously mapped memory.

### Args
- `arg0`: base address of modified region. This is rounded down to the nearest native page.
- `arg1`: number of bytes to modify, this will round up to the nearest native page.
- all other args ignored.

### Returns:
- `arg0`: base address of modified region.
- `arg1`: number of bytes modified.

### Notes:
- This will affect any regions or parts of inside of the specified region. Any memory inside the specified area, that is not already mapped will be ignored.

## 0x13 - MemoryMapFile
//TODO: 

## 0x14 - UnmapFile
//TODO:

## 0x15 - FlushMappedFile
//TODO:


# 0x2* - Device Management

## 0x20 - GetPrimaryDeviceInfo
//TODO:

## 0x21 - GetDevicesOfType
//TODO:

## 0x22 - GetDeviceInfo
//TODO:


# 0x3* - Filesystem Operations
The filesystem uses a single root style (unix, rather than NT), and URL-style protocols are not currently supported (and will return an error).

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

## 0x36 - SetIoControl


# 0x4* - Inter-Process Communication
Two flavours of IPC are supported: discrete messages (or packets, mailbox style) and continuous streams (memory buffers style). Access to IPC is described using a 4bit integer, occupying bits 63:60 of the flags argument. The access list for an ipc endpoint can be modified using the `ModifyIpcConfig` system call.
Ids can be either thread or process ids, with a process id allowing all it's threads the same access.
They accept the following values:
- `0`: No read/write allowed at all. Effectively disables this operation on the IPC endpoint.
- `1`: Public. Any process can read or write to this endpoint.
- `2`: Selected only. Only pre-approved process ids can access this endpoint. This list of ids starts empty and can be modified by the ModifyIpcConfig syscall.
- `3`: Private. This forces 1-to-1 communication, only a single remote process can access this endpoint.

For stream-based IPC, the server can start or stop a listener, while a number of clients can open or close connections to said server. Sending data is done using the read/write ipc syscalls. 
Streams are named, but as IPC subsystem is separate to the vfs there are no conflicts. This also means you cannot access IPC streams from the filesystem.

For message-based IPC, the server creates a mailbox with a callback function for when messages are received, and clients can use PostToMailbox to send messages. The server can then destroy the mailbox when it is done. Posting to an non-existent mailbox returns an error.

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

## 0x47 - DestroyMailbox

## 0x48 - PostToMailbox

## 0x49 - ModifyIpcConfig
Configures an existing IPC stream or mailbox. Args 2 and 3 change their function depending on arg0, which determines the operation to perform. Arg1 is the ipc endpoint (stream or mailbox) to operate on. The currently supported operations (`arg0`) are listed below:

- 0: reserved.
- 1: add an ID to the ipc access list. `arg2` is the id.
- 2: remove an ID from the ipc access list. `arg2` is the id.
- 3: change the ipc access flags. `arg2` is the new flags (in bits 60:63)
- 4: transfer ownership. `arg2` is the process id of the new owner. 

### Returns:
- Nothing.

### Notes:
- Only the owner of the endpoint can use this system call. This id has a process-level granuality (i.e. any thread in the owner process is granted owner-level access).

# 0x5* - General Utilities
This gorup of system calls is a collection of unrelated utilties.

## 0x50 - Log
Writes a log entry to the global log.

### Args:
- `arg0`: pointer to a c-string containing the text to be logged.
- `arg1`: level of log to be written: 0 = info, 1 = warning, 2 = error, 4 = verbose.
- all other args ignored.

### Returns:
- Nothing.

### Notes:
- Unlike the kernel function `logf()` this syscall does not provide any formatting support for security reasons. It writes the input string to the currently active log backendds verbatim.
- The logging level values mirror those in the kernel logging system, with the exception of the `fatal` level not being available. Any attempts to issue a fatal log result in the log being emitted as an error instead.
