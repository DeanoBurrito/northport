![Huge stylish northport banner](docs/images/banner.png)

![All builds CI badge](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml/badge.svg) ![](https://tokei.rs/b1/github/DeanoBurrito/northport?category=code)

Northport is a monolithic kernel written in C++ and booted via the Limine protocol. It currently supports x86_64 and riscv64 (via a custom boot shim). This repo also contains some supporting libraries, docs and source for the associated manual.

Instructions for building it yourself are [available here](docs/Building.md), and a more in-depth manual can be found under the releases tab or by cloning this repository and running `make docs`.

A brief summary of the current goals and features are listed below, or check [the roadmap](docs/Roadmap.md) for a more granular view.

## Project Goals
1) To build a modern and relatively complete kernel: driver infrastructure, graphics/audio/network stacks, VFS, and smp-aware scheduler.
2) Support for multiple platforms. My plan is to develop for risc-v first, and use the x86_64 port as a sanity check. With limine now supporting aarch64, support for that may come later.
3) To eventually be self-hosting, and have the system be able to cross-compile itself.
4) Clean code and useful documentation.
5) A comfortable (if limited) userspace experience: window manager, common applications like a text editor and file explorer. This is an extended goal.

## Project Features
Kernel:
- Support for multiple architectures: riscv64, x86_64.
- Memory management:
    - PMM with zoned allocations and support for adding additional memory at runtime.
    - Modular VMM (inspired by old SunOS design) with demand paging, and separate MMU layer.
    - General purpose heap provided by slabs for smaller objects and a freelist for larger objects.
- Logging infrastructure: fast and lock-free with support for several early outputs (uart chips, debugcon) and a built-in graphical terminal (based on gterm from the Limine bootloader).
    - Panic sequence with stack walker and symbol lookup.
- Global software clock, driven by a number of supported hardware timers: LAPIC, TSC, HPET, PIT, SBI timer.
- SMP-aware scheduler: round robin with per-core queues, work stealing and DPCs.
- VFS with a robust tempfs driver. File contents are sparsely cached in a global page cache.
- Loadable drivers and device management, partially automated by a device tree parser and PCI enumeration.
    - Notable drivers include: NVMe, virtio devices.
- Optional UB sanitizer, helpful for detecting bugs or increasing code size!

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
