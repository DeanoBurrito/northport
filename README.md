![Huge stylish northport banner](docs/images/banner.png)

![All builds CI badge](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml/badge.svg) ![](https://tokei.rs/b1/github/DeanoBurrito/northport?category=code)
![Latest Release Version](https://img.shields.io/github/v/tag/deanoburrito/northport?label=Latest%20Release&style=plastic)

Northport is a monolithic kernel, written in C++ and booted via the Limine protocol. It currently supports x86_64 and riscv64 machines. This repo also contains a completely standalone support library (see `libs/np-syslib`).

Instructions for building it yourself are [available here](docs/Building.md), and a more in-depth manual can be found under the releases tab or by cloning this repository and running `make docs`.

A brief summary of the current goals and features are listed below, or check [the roadmap](docs/Roadmap.md) for a more granular view. Pre-built isos are made available at major feature milestones, but for the latest and greatest it's recommended to build from the master branch.

## Project Goals
1) To build a usable, extensible and relatively complete kernel. This means an smp-aware scheduler with support for heterogeneous processors, media stacks (graphics, audio and network) and a flexible driver infrastructure.
2) Support for multiple architectures: the current targets are x86_64 and riscv64, but this may expand over time.
3) Be self hosting, this includes cross compiling the system.
4) Clean code and useful documentation.
5) Usable on both virtual machines and real hardware.

An extended goal (previously goal #5) is to add a comfortable user experience: a window manager, basic programs like a text editor and file explorer.

## Current Project Features
Kernel:
- Support for multiple architectures: riscv64, x86_64.
- Memory management:
    - Hybrid PMM, freelist for single page allocations and a slow path for allocations with constraints.
    - Modular and portable VMM, with demand paging options for anon and vfs backed memory. Inspired by the old SunOS design.
    - General purpose heap provided by slabs for smaller objects and a freelist for larger objects. Slab allocation can be accelerated by core-local caches, and several debug features are available.
- Time management:
    - Global software clock, driven by a number of hardware timers: LAPIC, TSC, HPET, PIT, SBI timer.
    - Run levels, allowing kernel to selectively mask groups of interrupts and prevent execution being hoisted by the scheduler. This also means support for DPCs and APCs.
    - Scheduler with core-local and shared work queues, and work stealing.
    - Waitable events, with support for cancelling, timeouts and waiting on multiple events at once.
- Logging infrastructure: fast and mostly lock-free with support for several early outputs (uart chips, debugcon) and a built-in graphical terminal (based on gterm from the Limine bootloader).
    - Panic sequence with stack walker and symbol lookup.
- VFS with a robust tempfs driver, and full API for drivers.
    - File contents are sparsely cached (as needed) in a page cache.
    - File metadata and VFS nodes are also cached as needed.
- Driver management: with the exception of timers and interrupt controllers, all device functionality is provided by loadable kernel modules:
    - Drivers are organised in a tree-like structure, allowing parent drivers to provide functionality (like transport I/O) to child drivers.
    - Notable drivers include: pci(e), some VirtIO devices and NVMe.
- Optional UB sanitizer, helpful for detecting bugs or increasing code size!

Build System:
- Uses stock core tools and GNU make, runs anywhere (tm).
- Xorriso is needed for generating bootable isos.

## Glorious Screenshots
![](https://github.com/DeanoBurrito/northport/assets/12033165/4ae74153-07c7-4896-846d-ead44fc956fe)
*14/02/2024: Status bar showing virtual memory and driver statistics*

![](https://github.com/DeanoBurrito/northport/assets/12033165/bc3cb9a0-5911-46a0-9837-e76a1f9ea86d)
*14/02/2024: Device node tree being printed shortly after adding the IO manager.*

<details>
<summary>Older Screenshots</summary>

![](https://github.com/DeanoBurrito/northport/assets/12033165/95c61e2b-3c8e-435c-8ee4-6e066e29fb0a)
*11/10/2023: Kernel panic while loading a malformed driver from the initdisk.*

![](https://user-images.githubusercontent.com/12033165/202898511-7e10e72c-6cfa-4f30-b7a5-3173dac36199.png)
*20/11/2022: x86 and riscv kernels running side by side in qemu.*
</details>

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): another 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!

This is a complete rewrite of the original. If you're looking for that, it's available on the `archive-2022-version` branch.
