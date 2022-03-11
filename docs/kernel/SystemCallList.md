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

## 0x1 - GetPrimaryDevice
Attempts to get details on the current primary device for a device type. Think of this as getting the default for a certain operation. For example, the primary framebuffer is usually being output to the main monitor. This is what you want for lazy IO.

### Args:
- `arg0`: info details. 0 = basic (returned in regs), 1 = advanced (returned as memory block).
- `arg1`: device type. Corresponds to entries of DeviceType enum found in GenericDevice.h.
- all other args ignored.

### Returns:
For requests of type GraphicsFramebuffer, in basic mode: 
- `arg0`: base address
- `arg1`: bits 31:0 = width in pixels, bits 63:32 = height in pixels
- `arg2`: bits 31:0 = bits per pixel, bits 63:32 = bpp (bytes per row/stride)
- `arg3`: first 4 bytes are left shifts for red/green/blue/reserved subpixels. Upper 4 bytes are masks for r/g/b/R subpixels.

Requests of type GraphicsAdaptor are currently unsupported.

---
For advanced detail requests, all devices will return a pointer to a struct in `arg3`. This struct will be on the type `{DeviceType}AdvancedInfo`. 
For example a request for a device of `FramebufferDevice` would return a pointer to `FramebufferDeviceAdvancedInfo` somewhere within the program's memory space.

### Notes:
- None.

# 0x1* - Mapping Memory and Files
The following functions relate to mapping/unmapping memory regions for a given program. These can only affect other regions of the same program, previously requested by the program. Regions allocated by the system (shared buffer, and the program binary itself) are protected from changes in this manner.
Userspce programs can only map memory in the lower halt

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
