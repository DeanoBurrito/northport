# System Calls

## Overview
This document and it's supporting [list of system calls](SystemCallList.md) doc detail the interface between userspace and the kernel.

Of course, while you *can* program against the ABI, there is a wrapper library `np-syscall` that implements more familiar C++ functions and types to make these calls much more forgiving, and less error-prone.
Using `np-syscall` is highly recommended over using the ABI directly, unless absolutely required.

The ABI consists of 5 registers (see platform specific section below, for what they map to):
| Name | Use as input (user -> kernel) | Use as output (kernel -> user) |
|------|-------------------------------|--------------------------------|
| `id` | Syscall number (see [list](SystemCallList.md))     | Status code. 0 for success, 0x404 for invalid syscall number. |
|`arg0`| 1st argument (primary)        | 1st return value (primary)     |
|`arg1`| 2nd argument                  | 2nd return value               |
|`arg2`| 3rd argument                  | 3nd return value (count of overflow values) |
|`arg3`| 4th argument (address of callback, if applicable) | 4th return value (pointer to overflow values) |

These registers are expected to be at least 64 bits wide. They may be wider, but only the lower 64 bits will be utilised, with the higher bits being ignored.

If an array of data needs to be returned, `arg3` will contain a pointer to a tightly packed array of the returned data type, with `arg2` containing the number of elements packed into the array. This region of memory is read only to the user program, and the other return values are free for use.
If a structure larger than the combined 256-bit value of the registers needs to be returned, a pointer to a read-only region of memory will be returned in `arg3`.

Registers that arent used by a syscall as input are ignored, and return registers that arent used will be zeroed. All other registers besides the 5 used have their values preserved during a syscall.

## Endianness
For portability reasons, the kernel will layout multi-byte integers and structures in the native endianness of the cpu the kernel is running on. Therefore any data larger than 1 byte should 'just work' on whatever platform you're running on.

For data that is transported through a system call from an external source (like a drive, or the network), the transported data remains untouched and the kernel makes no guarantees about its byte order. Check the source's documentation on that.

## x86_64 ABI
The x86_64 ABI is loosely inspired by the System V ABI, and should be familiar enough to jump in.

The registers are mapped to:
- `id` is `rax`. Think of it as the this->pointer is c++.
- `arg0` is `rdi`.
- `arg1` is `rsi`.
- `arg2` is `rdx`.
- `arg3` is `rcx`.

To initiate a system call, any program can use `int 0x24` (as codified in [Platform.h](../../kernel/include/Platform.h)). This will perform the switch to the kernel syscall handler.

## RISC-V 64 ABI
The RV64 ABI follows the standard calling convention proposed by the foundation, with the exception of `x12/a2`, `x13/a3` and `x14/a4` also being used for return values.

The registers are mapped to:
- `id` is `x10/a0`.
- `arg0` is `x11/a1`.
- `arg1` is `x12/a2`.
- `arg2` is `x13/a3`.
- `arg3` is `x14/a4`.

To initiate a system call a program can use the `ecall` instruction.
