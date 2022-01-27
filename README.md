![](https://tokei.rs/b1/github/deanoburrito/northport)

# Northport
Northport is a monolithic kernel and utilities, targeting x86_64.
It's booted via the stivale2 protocol (using limine).

Limited docs about the kernel and other sub-projects are contained under [`docs/`](docs/), all available in markdown format. These are a work in progress, and will be expanded over time (haha).

For instructions on building and running northport, check [here](docs/Building.md).

# Project layout
Each of sub-projects are in their own folder:
- `docs/`: documentation for various parts of the project. 
- `initdisk/`: the init ramdisk for the kernel. All the files included are stored here.
- `iso/`: is where the final iso is built, not included in the git repo.
- `kernel/`: the kernel itself. There's also the `kernel/arch/` dir, which contains cpu specific code.
- `misc/`: contains unrelated project files, limine.cfg lives here.
- `libs/np-xyz`: contains project files for northport (np) library xyz.
- `libs/build`: built libraries are stored here, not included in the git repo for obvious reasons.

Most sub-projects share a common internal layout:
- `project_name_here/include/`: header files in here, making installing them later really easy.
- `project_name_here/build/`: not included in git repo, but where build files are stored.

# Project Goals
## Planned Features
A relatively competent scheduler, meaning it's SMP-aware (check!) and can gracefully handle async syscalls.

I'd also like to implement (or at least port) a libc, and get a custom compiler target for gcc setup, so I can start porting software over (looking at you doom).

A *nice*, if limited, userspace experience. Basic window manager, shell, and other common programs like a file explorer and text editor.

A port to another platform is something I've been interested in for a while. I'm currently looking at a 64 bit riscv platform. It's seems quite interesting, and definitely doable.

Decent documentation! It'd be nice to induct some other people into developing small programs or drivers in the future.

## Current Features
- Physical memory manager (does page frame allocation, in single or multiple pages).
- Paging support (4 or 5 levels).
- GDT and IDT implementations. IDT implementation is nothing unique, but something I think is quite cool.
- Simple heap for the kernel. It's a linkedlist style heap for now.
- Support for simple devices: IO/APIC, Local APIC, PS2 controller/keyboard/mouse, 8254 PIT.
- Scheduler, with support multiple cores.
- Stack traces. These can be printed with symbol names of the currently running program (including kernel), and will demangle c++ names.
- Custom string and string formatting implementation (that conforms to printf() style).
- Linear framebuffer and character-based framebuffer. PSF v1 & v2 rendering.
- PCI support, both legacy and ECAM.
- Loadable driver infrastructure, allowing device drivers to be loaded as devices are discovered.
- Support libraries: np-graphics (for simple cpu-driven graphics), np-syscall (syscalls c++ wrapper), np-syslib (utilties library, partial STL implementation).

## Screenshots
Currently not much to see, as there's no gui. Have a boot log for now.

![Northport development bootlog](docs/assets/northport-boot-log-nover.png)
