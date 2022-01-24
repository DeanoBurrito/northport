# Building Northport
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

### Setting up the build environment (Clang)
Clang requires no setup to use. As long as it's installed (and llvm too, llvm-ar is used), it's ready to go.

### Setting up the build environment (GCC)
If you're developing on an x86_64 you *can* build with your native GCC, but it's not recommended.
The osdev wiki has a walkthrough on building a gcc cross compiler from scratch [here](https://wiki.osdev.org/GCC_Cross-Compiler).
For reference, compiling GCC and binutils takes ~10 minutes for me, on mid-range laptop from 2017.

After this is done, you'll need to open the root makefile and edit either `TOOLCHAIN_DIR` or the individual binary paths (`CXX, AR, ASM, LD`) to point to your new binaries. After this you should be good to go.

### Make targets
The full list of make targets are below:
- `make all`: by default it builds everything, and creates a bootable iso in `iso/`.
- `make clean`: as you'd expect, removes build files and forces a clean build.

- `make run`: builds everything, creates an iso and launches qemu with the iso.
- `make debug`: same as run, but it starts a gdb server on port 1234, and waits for a connection before starting execution.
- `make attach`: a convinient way to have gdb attach to a waiting vm, and load kernel symbols.

There are also a few useful settings under the build config section (in the root makefile):
- `OPTIMIZATION_FLAGS`: does what you'd expect, just a macro with a nice name.
- `INCLUDE_DEBUG_INFO`: set to true to have gcc add debug info to the kernel, false if not needed.

# Build flags
There are a number of flags that can be defined at compile time to enable/disable certain behaviours.

<details>
    <summary>General Flags</summary>

- `NORTHPORT_PCI_FORCE_LEGACY_ACCESS`: PCI subsystem will ignore the machine config acpi table (if available), and only use the legacy ports
- `NORTHPORT_SUPPRESS_UBSAN_TYPE_MISMATCH`: When kernel undefined behaviour sanitizer is active, will suppress type-mismatch messages (quite spammy).
</details>

<details>
    <summary>Logging Flags</summary>
    
- `NORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT`: enables logging over debugcon, useful for debugging early boot in VMs.
- `NORTHPORT_ENABLE_FRAMEBUFFER_LOG_AT_BOOT`: enables logging directly to framebuffer. Messy, but it works.
</details>

<details>
    <summary>Debugging Helpers</summary>
    
- `NORTHPORT_DEBUG_USE_HEAP_CANARY`: kernel heap is compiled with a 'canary' value and associated functions. Uses an extra uint64_t per allocation, and extra time during allocations and frees (its some simple bitwise logic, it's still non-zero processing time). It cant repair the linked list, but can be helpful for tracking down buffer overruns and issues in the heap itself.
- `NORTHPORT_DEBUG_DISABLE_SMP_BOOT`: disables starting up all cores except the bsp at boot-time. They're currently completely unused if this is enabled. Useful for diagnosing multi-core issues.
</details>
