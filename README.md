![Huge stylish northport banner](docs/images/banner.png)

![All builds CI badge](https://github.com/DeanoBurrito/northport/actions/workflows/build-tests.yml/badge.svg) ![](https://tokei.rs/b1/github/DeanoBurrito/northport?category=code)
![Latest Release Version](https://img.shields.io/github/v/tag/deanoburrito/northport?label=Latest%20Release&style=plastic)

This repo contains the northport kernel, some support libraries and a collection of drivers for the kernel to use. The kernel is booted via the limine protocol on 64-bit platforms, and has support for generic x86_64 and riscv64 machines, as well as the qemu virt-m68k virtual machine.

Pre-built isos are available with each release, and instructions for building it from source yourself are available in [docs/Building.md](docs/Building.md). Further documentation is available in the manual, which can generated by running `make docs` in the repository root. *Please note that the documentation is currently quite outdated for the current source code.*

## Project Goals
1) To build a usable, extensible and relatively complete kernel.
2) Support for multiple architectures: initial targets were x86_64 and riscv64, but m68k has already been added. More to come!
3) Be completely self hosting, including cross compiling the system.
4) Legible code and useful documentation.
5) Usable on both virtual machines and real hardware.

An extended goal (previously goal #5) is to add a comfortable user experience: a window manager, basic programs like a text editor and file explorer. This has been moved to a separate project.

## Current Project Features
Kernel:
- Support for multiple architectures: x86_64, riscv64, m68k.
- Memory management:
    - Hybrid PMM, freelist for single page allocations and a slow path for allocations with constraints.
    - Modular and portable VMM, with demand paging options for anon and vfs backed memory. Inspired by the old SunOS design.
    - General purpose heap provided by slabs for smaller objects and a freelist for larger objects. Slab allocation can be accelerated by core-local caches, and several debug features are available.
- Time management:
    - Global software clock, driven by a number of hardware timers: LAPIC, TSC, HPET, PIT, SBI timer.
    - Run levels, allowing kernel to selectively mask groups of interrupts and prevent execution being hoisted by the scheduler. This also means support for DPCs and APCs.
    - Scheduler with core-local and shared work queues, and work stealing.
    - Waitable events, with support for cancelling, timeouts and waiting on multiple events at once.
- Filesystem:
    - File contents and metadata are sparsely cached in memory as needed. VFS nodes can also be destroyed and re-created as needed.
    - Robust tempfs driver provided within the kernel.
- Drivers:
    - Loaded (and dynamically linked) on-demand, based on load conditions described in driver metadata.
    - Drivers, device descriptors and device APIs are organised in a tree, allowing parent drivers to provide functionality to child drivers and device nodes.
    - Dedicated driver API functions, based on C-language API for the target platform - meaning drivers can be written in any language, although all first-party drivers are C++.
- Debugging:
    - Lock-free logging infrastructure, with per-core log buffers for maximum speed. Support for early log outputs via simple devices like uart chips or the built-in graphical terminal (based on gterm for Limine).
    - UB sanitizer, helpful for catching bugs or increasing code size.
    - Stack walker and symbol lookup, including symbols of loaded drivers. De-mangling C++ names not currently supported.

- Drivers:
    - `pci`: provides bus enumeration and access to the kernel.
    - `qemu`: for manipulating emulated display and simple system power control (shutdown, reboot).
    - `nvme`: only supports PCI-based controllers, reading and writing supported (tested on real hardware).
    - `fwcfg`: exposes qemu's fw_cfg interface as a filesystem and allows the kernel to use ramfb as a graphics adaptor.
    - `uacpi`: provides an aml interpreter and other acpi runtime services to the kernel, like enumerating devices not discoverable via other means. [Upstream repo can be found here.](https://github.com/UltraOS/uACPI)

## Glorious Screenshots

![](https://github.com/DeanoBurrito/northport/assets/12033165/f45c2584-d916-4b46-8f45-dd277d8843d4)
*02/06/2024: General state of the kernel after incremental changes.*

![](https://github.com/DeanoBurrito/northport/assets/12033165/834686bb-0978-4ecd-9f97-d54869f21f16)
*02/06/2024: Kernel panic, showing symbol lookup for kernel and loaded drivers.*

<details>
<summary>Older Screenshots</summary>

![](https://github.com/DeanoBurrito/northport/assets/12033165/1446c6a7-2cf7-4031-93cd-eac6a164ed8b)
*02/06/2024: Driver tree being shown after booting on my framework 13*

![](https://github.com/DeanoBurrito/northport/assets/12033165/4ae74153-07c7-4896-846d-ead44fc956fe)
*14/02/2024: Status bar showing virtual memory and driver statistics*

![](https://github.com/DeanoBurrito/northport/assets/12033165/bc3cb9a0-5911-46a0-9837-e76a1f9ea86d)
*14/02/2024: Device node tree being printed shortly after adding the IO manager.*

![](https://github.com/DeanoBurrito/northport/assets/12033165/95c61e2b-3c8e-435c-8ee4-6e066e29fb0a)
*11/10/2023: Kernel panic while loading a malformed driver from the initdisk.*

![](https://user-images.githubusercontent.com/12033165/202898511-7e10e72c-6cfa-4f30-b7a5-3173dac36199.png)
*20/11/2022: x86 and riscv kernels running side by side in qemu.*
</details>

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): another 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!

This is a complete rewrite of the original. If you're looking for that, it's available on the `archive-2022-version` branch.
