# np-syslib (Northport System Library)

Syslib is statically linked, containing all sorts of utility code. It's written with both kernel and userspace in mind.

Syslib has no dependencies other than `stddef.h`, `stdint.h`, and an implementation for `malloc(size_t size)` and `free(void* where)`.
The functions are resolved at link-time, so as long as they exist *somewhere*, that's enough.

## Common

Syslib implements a number of common standard library functions, and headers:
- [Algorithms.h](../../libs/np-syslib/include/Algorithms.h)
- [CppStd.h](../../libs/np-syslib/include/CppStd.h): utils for template programming.
- [Maths.h](../../libs/np-syslib/include/Maths.h)
- [Memory.h](../../libs/np-syslib/include/Memory.h): the usual functions, and some fun extras
- [Utilities.h](../../libs/np-syslib/include/Utilities.h)
- [Optional.h](../../libs/np-syslib/include/Optional.h)
- [String.h](): this is a c++ style string class, not the c header.

## Containers
A collection of useful templated container types are stored in `syslib/include/containers/`. The classic and well known `Vector` and `LinkedList` types are there, as are some more systems focused ones like a fixed-size circular queue.

- [CircularQueue.h](../../libs/np-syslib/include/containers/CircularQueue.h)
- [LinkedList.h](../../libs/np-syslib/include/containers/LinkedList.h)
- [Vector.h](../../libs/np-syslib/include/containers/Vector.h)

## Elf Helpers
`syslib/include/elf` contains useful code for getting symbols from ELF64 headers (elf32 not supported currently), as well as a C++ name demangler. Demangler is currently WIP, but covers most of the basic cases.

- [ELF64 spec](../../libs/np-syslib/include/elf/Elf64.h)
- [HeaderParser.h](../../libs/np-syslib/include/elf/HeaderParser.h)
- [Demangle.h](../../libs/np-syslib/include/elf/Demangle.h)

## File Format Helpers
`syslib/include/formats` contains files relating to specific file formats. Some are listed below.

- [XPixMap.h](../../libs/np-syslib/include/formats/XPixMap.h)
- [Tar.h](../../libs/np-syslib/include/formats/Tar.h): both unix-standard and posix-extended tar formats.

## Miscellanious
- [NativePtr.h](../../libs/np-syslib/include/NativePtr.h): Useful helper for converting between pointes and addresses in c++. 
- [IdAllocator.h](../../libs/np-syslib/include/IdAllocator.h): Templated allocator for unique ids.
