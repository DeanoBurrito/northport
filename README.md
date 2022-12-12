![Huge stylish northport banner](docs/images/banner.png)

![All Builds](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml/badge.svg) ![](https://tokei.rs/b1/github/DeanoBurrito/northport?category=code)

Northport is a monolithic kernel, with some supporting libraries and utilities. The kernel is booted via the limine protocol, and currently has support for x86_64 and riscv64 systems.

Instructions for building it yourself are [available here](docs/Building.md). A proper manual is WIP and will be available under `docs/manual`.

A brief summary of the current goals and features are listed below, check [the roadmap](docs/Roadmap.md) for a more granular view.


## Project Goals
1) To build a modern and relatively complete kernel: driver infrastructure, graphics/audio/network stacks, VFS, and smp-aware scheduler.
2) Support for multiple platforms. My plan is to develop for risc-v first, and use the x86_64 port as a sanity check. With limine now supporting aarch64, support for that may come later.
3) To eventually be self-hosting, and have the system be able to cross-compile itself.
4) Clean code and useful documentation.
5) A comfortable (if limited) userspace experience.

## Project Features
Kernel:
- Support for multiple architectures: riscv64, x86_64.
- Memory management: Bitmap based PMM with zoned allocations, VMM inspired by SunOS design, and a swappable heap provided by slabs and a free-list for larger items.
- Logging with support for various backends: serial/debugcon and built-in terminal (requires a framebuffer).
    - The terminal is based on gterm from the Limine Bootloader (see the individual files for the license).
- Optional UB sanitizer, helpful for detecting bugs or increasing code size!
- Support for various platform-specific timers, soft-timer interface on top.
- SMP-aware scheduler: round robin with per-core queues and work stealing.
- Loadable drivers and device management.

Build System:
- Uses stock core tools and GNU make, runs anywhere (tm).
- Xorriso and limine are needed for creating a bootable iso.

## Glorious Screenshots
![Screenshot_20221120_215230](https://user-images.githubusercontent.com/12033165/202898511-7e10e72c-6cfa-4f30-b7a5-3173dac36199.png)
*20/11/2022: x86 and riscv kernels running side by side in qemu.*

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): another 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!

This is a complete rewrite of the original. If you're looking for that, it's available on the `archive-2022-version` branch.
