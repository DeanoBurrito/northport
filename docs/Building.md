# Building Northport

This should cover everything needed to build, run and debug the kernel. If you encounter problems, please let me know by opening an issue or pinging me on discord. Thanks!

## Pre-Requisites

The following tools are required:
- Core utils, your system should already have these installed: `cp`, `mv`, `rm` and `mkdir`.
- For non riscv architectures, you'll need `xorriso` and a copy of the [limine bootloader](https://github.com/limine-bootloader/limine). See their install instructions for getting setup with limine. This is usually just cloning the latest binary branch and running `make` to build a copy of the `limine-deploy` tool for your system.
- A compiler: Either a gcc cross compiler or clang is required. Building with clang for riscv is currently not supported. For gcc, your target triplet should be `x86_64-elf` or `riscv64-elf` to use the default setup, but you can override this if yours differs.
- For testing you will need qemu, and `gdb-multiarch` is required for debugging.

At this point your system is setup, you'll want to edit some variables in `Config.mk` before building:
- `TOOLCHAIN`: Select 'gcc' or 'clang'.
- `TOOLCHAIN_PREFIX`: Only for gcc, set this to where you installed your cross compiler.
- `TOOLCHAIN_SYSROOT`: Set this to your compiler's sysroot folder. Normally this is under `lib/gcc/x86_64` or `lib/gcc/riscv64`, relative to where your cross compiler is installed.
- `LIMINE_DIR`: Where you cloned the limine repo.
- `CPU_ARCH`: The architecture to build for.

This file is used to store any user-facing settings. If you need to override the names used for the toolchain binaries, these are defined at the top of the root makefile. This is not the recommended way of doing this, but it is available should you need to.

## Supported Configurations

| Platform | GCC | Clang | Qemu | Hardware                  |
|----------|-----|-------|------|---------------------------|
| x86_64   | Yes | Yes   | Yes  | Partially working, 2 out of 3 tested computers will boot. |
| riscv64  | Yes | No    | Yes  | Untested, awaiting hardware. |

## Make Targets

All build commands should be run from the project root directory. Currently the following make targets are available:
- `make`, `make all`: Builds everything, will pack everything into an output specific to the platform. For x86_64 this means generating an iso with the kernel, and limine installed. For riscv this just builds the kernel.
- `make clean`: Removes any binaries and temporary build files. Forces the next build from to start from a clean slate.
- `make run`: Builds the project, and launches it in qemu for the target arch.
- `make run-kvmless`: Same as above, but will use software emulator instead of kvm.
- `make debug`: Builds the project, launches it in qemu. Qemu will launch a gdb server with default args (port 1234) and stop the cpu before running any code.
- `make attach`: Launches gdb with the kernel symbols loaded, connects to a gdb remote server, like one launched using `make debug`.
- `make docs`: Currently a no op, will build the documentation when available.
