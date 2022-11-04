**Please note: I'm in the middle of a big rewrite of the kernel, so its currently a bit chaotic. The previous master branch has been renamed to archive-2022-version if you were looking for that.**

[![All Builds](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml/badge.svg)](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml) [![](https://tokei.rs/b1/github/DeanoBurrito/northport?category=code)](https://github.com/DeanoBurrito/northport)

# Northport
Northport is a monolithic kernel, with some supporting libraries and utilities.
It's booted via the limine protocol, and supports riscv64 and x86_64. 

For instructions on building, [check here](docs/Building.md). Documentation is WIP and will be available in the `docs/manual` directory.

A brief summary of the current and planned features are available below, but for a more in-depth roadmap can be found [here](docs/Roadmap.md).

## Project Goals
1) To build a modern and relatively complete kernel: driver infrastructure, graphics/audio/network stacks, VFS, and smp-aware scheduler.
2) Support for multiple platforms. My plan is to develop for risc-v first, and use the x86_64 port as a sanity check. With limine now supporting aarch64, support for that may come later.
3) To eventually be self-hosting. The system should be able to cross-compile itself.
4) Clean code and useful documentation.
5) A comfortable (if limited) userspace experience.

## Project Features
Kernel:
- Support for multiple architectures: riscv64, x86_64.
- Memory management: 
    - PMM (bitmap based) supporting multiple zones. 
    - VMM insprired by the old SunOS design. VM ranges are backed by drivers specific to the allocation type: file-caching, working memory (anon), mmio.
    - Kernel heap uses slabs for smaller allocations (32 - 512 bytes), with a free-list for anything larger.
- Logging with support for various backends: serial/debugcon and built-in terminal (requires a framebuffer).
    - The terminal is based on gterm from the Limine Bootloader (see the individual files for the license).
- Optional UB sanitizer, helpful for detecting bugs or increasing code size!
- Support for various platform-specific timers, soft-timer interface on top.
- SMP-aware scheduler: round robin with per-core queues.

Build System:
- Uses stock core tools and GNU make. Runs anywhere (tm).
- Xorriso and limine are needed for creating a bootable iso.

## Glorious Screenshots
*Coming soon (tm).*

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): another 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!
