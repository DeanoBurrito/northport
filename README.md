**Please note: I'm in the middle of a big rewrite of the kernel, so its currently a bit chaotic. The previous master branch has been renamed to archive-2022-version if you were looking for that.**

# Northport
Northport is a monolithic kernel, with some supporting libraries and utilities.
It's booted via the limine protocol, and supports riscv64 and x86_64. 

For instructions on building, [check here](docs/Building.md). Documentation is WIP and available in the `docs/manual` directory.

A brief summary of the current and planned features are available below, but for a more in-depth roadmap can be found [here](docs/Roadmap.md).

## Project Goals
1) Modern and relatively complete kernel: driver infrastructure, graphics/audio/network stacks, VFS, and smp-aware scheduler.
2) Cross-platform. I'm developing for riscv64 first, with the x86_64 port used as a sanity check.
3) To be self-hosting. The system should be able to perform a cross-platform build of itself.
4) Clean code and useful documentation.

## Project Features
Kernel:
- Memory management: PMM (bitmap based), VMM and kernel heap (slabs and buckets).
- Logging with support for various backends: serial/debugcon and built-in terminal (requires a framebuffer).
    - The terminal is based on gterm from the Limine Bootloader (license attached).
- Optional UB sanitizer, helpful for detecting bugs or increasing code size!

Build System:
- Uses stock core tools, and GNU make. Runs anywhere (tm).

## Glorious Screenshots
*Coming soon (tm).*

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): another 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!
