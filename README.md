![](https://tokei.rs/b1/github/deanoburrito/northport)

# Northport
Northport is a monolithic kernel and utilities, targetting x86_64. It's booted via the limine boot protocol. 
There are limited docs about the kernel and supporting libraries under `docs/`, all available in markdown format. These are very much WIP, so there may be sections that are unfinished or even missing.

For instructions on building and running, check [here](docs/Building.md). Down below there's a brief overview of the current feature-set, but for a roadmap of current and planned features, see [here](docs/Roadmap.md).

# Project Goals
## Planned Features
- A relatively complete core kernel: smp-aware scheduler, graphics, audio and network stacks. A vfs with drivers for a few filesystems, with some sort of memory cache. Asynchronous io would be nice, but that may be beyond my reach for now.

- A stable and reasonably fleshed-out userspace, including development tools, with the ultimate goal of being self-hosting. This also means we'll need a libc/libc++ port, and the associated supporting libraries. This project isn't aiming to become linux-compatible, or be posix-compliant. Those are just not interesting goals to me.

- A comfortable, if very limited, user experience. This means a basic window manager, gui (and terminal!) shell, and other some common programs like a text editor and file explorer.

- Another long term goal is a port for at least one other platform. Currently I'm looking at riscv64, even if its only the qemu virt machine for now.

- Decent (or at least *usable*) documentation. It'd be nice to get some other people involved in driver or program development later on!

## Current Features
Kernel:
- Physical memory manager (does page frame allocation, in single or multiple pages).
- Per-process virtual memory manager, with paging support for x86 (4 or 5 levels).
- GDT and IDT implementations. IDT implementation is nothing unique, but something I think is quite cool.
- Kernel heap: combination of slab and pool allocators, with some handy debug features.
- Support for simple devices: IO/APIC, Local APIC, PS2 controller/keyboard/mouse, 8254 PIT.
- Scheduler, with support for multiple cores. 
- PCI support, both legacy x86 and ECAM MMIO.
- Loadable drivers. Currently only a handful of simple drivers exist, more to come over time.
- Single-root style VFS, full support for mounting/unmount and file lookup. Currently only has the init ramdisk driver implemented.
- IPC, both stream and packet based. 
- System calls. Nothing too technically interesting, but an implementation I'm quite proud of.
- Optional UBsan, enabled in the kernel, disabled everywhere else for now.
- Logging layer. Can enable/disable the various backends at runtime. Support for debugcon currently, framebuffer and serial planned.

Support libraries:
- np-graphics: cpu-driven graphics, limited drawing functions.
    - A basic linear framebuffer, and terminal (character-based) renderer implementations. Both their own simple drawing functions.
    - A baked in psf1 & psf2 font, both supported by the terminal renderer.
    - QOI format decoder.
- np-gui: framework for building user interfaces.
    - Provides a wrapper around communication with the window server (also the reference implementation).
- np-syscall: friendly c++ wrapper around kernel system calls.
    - There is also the abi [description](docs/kernel/SystemCalls.md) and [spec](docs/kernel/SystemCallList.md) itself you want to go that route.
- np-syslib: utility library. It provides parts of the STL and std library I miss in a freestanding environment.
    - Printf-compatable string formatting, with a few extras.
    - String <-> number conversions.
    - Can generate stack traces, optionally parsing elf files (including the kernel!) for accompanying symbol names.
    - Some template containers (vector, linked list, circular queue).
    - General utilities, too small and many to list here. 
    - C++ name demangler, for use with stack traces.
- np-userland: standard library for every userspace program.
    - Currently only provides a heap allocator per process. More coming soon.

Build System:
- All built using stock unix tools, and GNU make. Runs anywhere (tm).

Native Applications:
- startup: currently a test app, but will setup a friendly user environment soon.
- server-window: soon to be a simple window manager, and compositor.

# Screenshots
Currently not much to see, as there is no functional GUI yet, for now you'll have to be satisfied with log outputs. These screenshots are captured from konsole, with the logs being output over debugcon (the port 0xE9 hack).

![Kernel heap initialization output](https://user-images.githubusercontent.com/12033165/173810490-8387e2ed-2d4c-4be8-bd0b-dab702b4aeaf.png)
- Here you can some stats about the kernel heap shortly after it's initialized. It's comprised of a number of slabs, increasing in size, and then a 'catch all' pool allocator for anything else.

![PCI discovery, loading userspace ELFs and a kernel panic.](https://user-images.githubusercontent.com/12033165/173810625-6209a8e3-d244-4149-8b6c-c6ca9523e802.png)
- Just showing some PCI device discovery, and then some userspace ELFs being loaded (those are the kernel heap expansion warnings). We can see the window server starting, and then a page fault to demonstrate the panic output.

# Project layout
Each of sub-projects are in their own folder:
- `docs/`: documentation for various parts of the project. 
- `initdisk/`: the init ramdisk for the kernel. All the files included are stored here.
- `iso/`: is where the final iso is built, not included in the git repo.
- `kernel/`: the kernel itself. 
    - `kernel/syscalls/`: each file contains the implementation of a the syscall group of the same name. It's done this way as each group will deal with vastly different functionality.
    - `kernel/arch/`: contains architecture specific code.
    - the other various folders contain code for a specific subsystem.
- `misc/`: contains unrelated project files, limine.cfg lives here.
- `libs/np-xyz/`: contains project files for northport (np) library xyz.
- `libs/build/`: built libraries are stored here, not included in the git repo for obvious reasons.
- `userspace/xyz/`: contains project files for a native app called xyz.

Most sub-projects share a common internal layout:
- `project_name_here/include/`: header files in here, making installing them later really easy.
- `project_name_here/build/`: not included in git repo, but where build files are stored.

# Related Projects
- [DreamOS64](https://github.com/dreamos82/Dreamos64): a 64-bit OS by one of the northport contributors, [Ivan G](https://github.com/dreamos82). 
- [OSdev notes](https://github.com/dreamos82/Osdev-Notes): a repository of notes about various osdev topics. Feel free to contribute!
