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
The root makefile defines all the tools used under the section 'toolchain selection',
so you can point it to your custom tools here, or what if you alreayd have a cross compiler folder, you can point `TOOLCHAIN_DIR` to it, and it'll take care of the rest.

The limine bootloader is used, and is expected to be at `TOOLCHAIN_DIR/limine` by default, but you can override this is your install is elsewhere.

To verify everything works you can run `make validate-toolchain`, it'll tell you if there are issues.

Building an iso is done by running `make iso`, and if qemu is installed, can be launched via `make run`.


### Make targets
The full list of make targets are below:
- `make all`: by default it builds everything, and creates a bootable iso in `iso/`.
- `make clean`: as you'd expect, removes build files and forces a clean build.
- `make run`: builds everything, creates an iso and launches qemu with the iso.
- `make debug`: same as run, but it halts the virtual machine and opens a gdb server on port 1234.
- `make attach`: a convinient way to have gdb attach to a waiting vm, and load kernel symbols.
- `make create-toolchain`: runs a script to install a cross compiler toolchain in the default location. 
- `make validate-toolchain`: validates the toolchain install.

There are also a few useful settings under the build config section (in the root makefile):
- OPTIMIZATION_FLAGS: does what you'd expect, just a macro with a nice name.
- INCLUDE_DEBUG_INFO: set to 1 to have gcc add debug info to the kernel, 0 if not needed.

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
