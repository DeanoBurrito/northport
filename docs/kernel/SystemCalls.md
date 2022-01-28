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
|`arg2`| 3rd argument                  | 2nd return value               |
|`arg3`| 4th argument                  | 4th return value               |

These registers are expected to be at least 64 bits wide. They may be wider, but only the lower 64 bits will be utilised.

If more data needs to be returned, `arg3` should contain a pointer to a syscall specific data strcture providing the rest of the data.

Registers that arent used by a syscall as input, are ignored, and return registers that arent used are in an undefined state (pls do not snoop private kernel data from these registers. Thank you.).

## x86_64 ABI
The x86_64 ABI is loosely inspired by the System V ABI, and should be familiar enough to jump in.

The registers are mapped to:
- `id` is `rax`. Think of it as the this->pointer is c++.
- `arg0` is `rdi`.
- `arg1` is `rsi`.
- `arg2` is `rdx`.
- `arg3` is `rcx`
