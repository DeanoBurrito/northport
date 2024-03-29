# Building Northport

This should cover everything needed to build, run and debug the kernel. If you encounter problems, please let me know by opening an issue or pinging me on discord. Thanks!

## Pre-Requisites

The following tools are required:
- Core utils, your system should already have these installed: `cp`, `mv`, `rm` and `mkdir`.
- You'll need `xorriso` to generate bootable iso images.
- A cross compiler: either a GCC cross compiler or clang is required. For GCC the target triplet is expected to be `x86_64-elf`, `riscv64-elf` or similar to use the default names. The binary names used can be changed in the root makefile  (at the very top) if yours is different.
- For testing you will need qemu, and `gdb-multiarch` is required for debugging.
- For building the manual, you'll also need `texlive`.

At this point your system has the all base tools required. It's recommend to check out `Config.mk` and set any options to your liking. This includes selecting which compiler toolchain to use, and pointing the build system to your toolchain binaries.

You'll also need to select whether to use the development file cache or not. This takes care of downloading limine and any uefi (required for booting in qemu). If you choose *not* to use the cache, you will need to make these files available to the build system, as explained in `Config.mk`.

## Supported Configurations

| Platform | Qemu | Hardware                  |
|----------|------|---------------------------|
| x86_64   | Yes  | Partially working, 2 out of 3 tested computers will boot. |
| riscv64  | Yes  | Untested, awaiting hardware. |

This table is updated on a best effort basis. Latest tests done with GCC 11 and clang 14 with qemu 7.0.0.

## Make Targets

All build commands should be run from the project root directory. Currently the following make targets are available:
- `make all`: Builds everything, will pack everything into an output specific to the platform. For x86_64 this means generating an iso with the kernel, and limine installed. For riscv this just builds the kernel.
- `make clean`: Removes any binaries and temporary build files. Forces the next build from to start from a clean slate.
- `make run`: Builds the project, and launches it in qemu for the target arch.
- `make run-kvmless`: Same as above, but will use software emulator instead of kvm.
- `make debug`: Builds the project, launches it in qemu. Qemu will launch a gdb server with default args (port 1234) and stop the cpu before running any code.
- `make attach`: Launches gdb with the kernel symbols loaded, connects to a gdb remote server, like one launched using `make debug`.
- `make docs`: Builds the documentation and renders it as a pdf.
- `make docs-clean`: Cleans build files related to documentation.
- `make cache-clean`: Remove cached development files (limine and ovmf firmware).
