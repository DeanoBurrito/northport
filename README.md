![](https://tokei.rs/b1/github/deanoburrito/northport)

# Northport
Northport is a monolithic kernel + utilities targetting x86_64.
It's booted using the stivale2 protocol (meaning limine is the bootloader),
and there's otherwise there's not much to say here yet. 

For my *wishful* plans for this project, see the project goals down below.

# Building
### Required
- Linux-like environment, WSL2 works, a full linux os is best though.
- A c++ cross-compiler with c++ 17 or greater support. The build system is setup for gcc.
- GNU make.
- xorriso.

### Recommended
- Qemu is required for using `make run` and gdb is needed for `make attach`.

### Setting up the build environment
The easy solution is to run `make create-toolchain`, which downloads gcc and binutils, and builds them for the target platform (x86_64).
It's worth noting this script assumes you're on a debian-based distribution (it uses apt).

Of course that's a huge waste of disk space if you have a GCC cross compiler installed.

The root makefile defines all the tools used under the section 'toolchain selection', so you can point it to your custom tools here, or what if you already have a cross compiler folder, you can point `TOOLCHAIN_DIR` to it, and it'll take care of the rest.

The limine bootloader is used, and is expected to be at `TOOLCHAIN_DIR/limine` by default, but you can override this if your install is elsewhere.

To verify everything works you can run `make validate-toolchain`, it'll tell you if there are issues.

Building an iso is done by running `make iso`, and if qemu is installed, can be launched via `make run`.

It's worth nothing I do occasionally build with clang (using `--target=`), but the project isn't intended to be built this way. No gaurentees are made about stability or even making it past compilation/linking.

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
- OPTIMIZATION_FLAGS: does what you'd expect, just a macro with a nice name.
- INCLUDE_DEBUG_INFO: set to true to have gcc add debug info to the kernel, false if not needed.

# Project layout
Each of sub-projects are in their own folder:
- `syslib`: a system utilities library. A big collection of code for both kernel and userspace programs.
- `kernel`: the kernel itself. There's also the `kernel/arch` dir, which contains cpu specific code.
- `initdisk`: the init ramdisk for the kernel. All the files included are stored here.
- `misc`: is unrelated project files, limine.cfg lives here.
- `iso`: is where the final iso is built, not included in the git repo.

Each project shares some common folder names:
- `project_name_here/include`: header files in here, making installing them later really easy.
- `project_name_here/build`: not included in git repo, but where build files are stored.

# Project Goals
I'd like to support smp and scheduling across multiple cores this time around,
and port the whole os to another platform (risc-v looks really interesting, and achievable).

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
</details>
