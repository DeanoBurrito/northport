![](https://tokei.rs/b1/github/deanoburrito/northport)

# Northport
Northport is a monolithic kernel and utilities targetting x86_64.
It's booted using the stivale2 protocol (meaning limine is the bootloader),
and there's otherwise there's not much to say here yet.

For my *wishful* plans for this project, see the project goals down below.

# Building
### Required Programs
- Linux-like environment, WSL2 works, a full linux os is best though.
- A c++17 or higher cross compiler. The build system is setup for clang (default), or GCC. It's easy to switch between.
- GNU make.
- Xorriso.
- [Limine](https://github.com/limine-bootloader/limine). I build and test with the latest 2.x release personally, check their install instructions. 

### Recommended Programs
- Qemu is required for using `make run` and gdb is needed for `make attach`.

### Setting up the build environment

The root makefile contains a few variables you will need to set for it to build correctly.
`LIMINE_DIR` will need to point to your limine install directory (where you pointer `git clone` to). If you're using the GCC toolchain, you'll also need to point `TOOLCHAIN_DIR` to the bin directory of your cross-compiler, or point each of lines for the tools to point there directly (see `CXX`, `ASM`, `LD`, `AR`).

There's some notes on compiler-specific setup below, and if you do switch toolchains, its always worth running `make clean` for a fresh build.

Once you're set up, you can build the project with `make`/`make all`, and generate a bootable iso with `make iso`.
If you're feeling adventurous you can execute `make run` to launch it qemu. Qemu monitor and debugcon will both share the stdin/stdout of the terminal used to run it.

Build-time options can be found under the 'build config' section in the root makefile. The full list of build flags are defined below.

### Setting up the build environment (GCC)
The easy out-of-the-box solution is the run `make create-toolchain`, which will download, build and then install a cross compiler for the default target platform (x86_64).
Of course that makes a lot of assumptions about where you want it installed, and is a huge waste of bandwidth and disk space if you already have a cross compiler installed.

The root makefile (located at `proj_root/Makefile`) has a toolchain selection section at the top. You can adjust the gcc binaries used, 
or you can just point `TOOLCHAIN_DIR` to your cross compiler bin folder.

If you're unsure of whether your toolchain is setup, you can test it with `make validate-toolchain`.

### Setting up the build environment (Clang)
Clang requires no setup to use. As long as it's installed (and llvm too, llvm-ar is used), it's ready to go.

### Make targets
The full list of make targets are below:
- `make all`: by default it builds everything, and creates a bootable iso in `iso/`.
- `make clean`: as you'd expect, removes build files and forces a clean build.

- `make run`: builds everything, creates an iso and launches qemu with the iso.
- `make debug`: same as run, but it starts a gdb server on port 1234, and waits for a connection before starting execution.
- `make attach`: a convinient way to have gdb attach to a waiting vm, and load kernel symbols.

- `make create-toolchain`: runs a script to install a cross compiler toolchain in the default location. 
- `make validate-toolchain`: validates the toolchain install.

There are also a few useful settings under the build config section (in the root makefile):
- `OPTIMIZATION_FLAGS`: does what you'd expect, just a macro with a nice name.
- `INCLUDE_DEBUG_INFO`: set to true to have gcc add debug info to the kernel, false if not needed.

# Project layout
Each of sub-projects are in their own folder:
- `syslib/`: a system utilities library. A big collection of code for both kernel and userspace programs.
- `kernel/`: the kernel itself. There's also the `kernel/arch/` dir, which contains cpu specific code.
- `initdisk/`: the init ramdisk for the kernel. All the files included are stored here.
- `misc/`: is unrelated project files, limine.cfg lives here.
- `iso/`: is where the final iso is built, not included in the git repo.

Each project shares some common folder names:
- `project_name_here/include/`: header files in here, making installing them later really easy.
- `project_name_here/build/`: not included in git repo, but where build files are stored.

# Project Goals
I'd like to support smp and scheduling across multiple cores this time around,
and port the whole os to another platform (risc-v looks really interesting, and achievable).

### Current Features
- Physical memory manager (does page frame allocation, in single or multiple pages).
- Paging support (4 or 5 levels).
- GDT and IDT implementations. IDT implementation is nothing unique, but something I think is quite cool.
- Simple heap for the kernel. It's a linkedlist style heap for now.
- Support for simple devices: IO/APIC, Local APIC, PS2 controller/keyboard/mouse, 8254 PIT.
- Parsing and finding ACPI tables.
- Scheduler, with support for kernel and userspace threads.
- Stack traces. These can be printed with symbol names of the currently running program (including kernel), and will demangle c++ names.
- Custom string and string formatting implementation (that conforms to printf() style).
- Linear framebuffer and character-based framebuffer. PSF v1 & v2 rendering.
- PCI support.

# Build flags
There are a number of flags that can be defined at compile time to enable/disable certain behaviours.

<details>
    <summary>Logging Flags</summary>
    These flags accept either `true` or `false`.
    
- `NORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT`: enables logging over debugcon, useful for debugging early boot in VMs.
- `NORTHPORT_ENABLE_FRAMEBUFFER_LOG_AT_BOOT`: enables logging directly to framebuffer. Messy, but it works.
</details>

<details>
    <summary>Debugging Helpers</summary>
    
- `NORTHPORT_DEBUG_USE_HEAP_CANARY`: kernel heap is compiled with a 'canary' value and associated functions. Uses an extra uint64_t per allocation, and extra time during allocations and frees (its some simple bitwise logic, it's still non-zero processing time). It cant repair the linked list, but can be helpful for tracking down buffer overruns and issues in the heap itself.
- `NORTHPORT_DEBUG_DISABLE_SMP_BOOT`: disables starting up all cores except the bsp at boot-time. They're currently completely unused if this is enabled. Useful for diagnosing multi-core issues.
</details>
