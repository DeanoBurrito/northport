# High Level Overview 

## Boot Sequence

The kernel currently has 3 main states during its lifetime: basic init, threaded init, and post init.

### Basic Init

*Authors note: This section of the kernel init is highly architecture (and platform) dependent, so the kernel will have separate entry points for each of these, however the general structure is described below.*

The kernel takes control of the machine from the bootloader and sets up a physical memory manager. There is only one per system, so this is the first thing to be started.
After this an initial set of page tables is constructed. These are modelled after the limine boot protocol.
- A hhdm (**h**igher **h**alf **d**irect **m**ap of physical memory in virtual space) address is decided. This is usually the lowest possible address in the higher half of the virtual address space. If booted via limine, we use the provided hhdm address. This changes whether 4 or 5 level paging is used.
- The first 4GB of physical memory is mapped at the hhdm.
- Any memory regions marked as 'usable', and above 4GB, are mapped into the hhdm, this includes most of ram.
- The entire kernel blob is mapped at 0xffff'ffff'8000'0000. 

Then the kernel heap is initialized, and so is formatted log output (since we can dynamically allocate now).

Next the kernel core is setup, things like an interrupt controller and its routing (idt on x86). Other platform specific stuff goes on here, like finding/parsing ACPI tables. The scheduler data is also initialized here, although it isnt started until after the other cores have joined the kernel in long mode.

To finish the first stage of the kernel init, all the other cpu cores are started and progress to 64-bit long mode. Then all cores setup their core local data structures (on x86: tss, gdt. Idt is actually shared between all cores), and respective interrupt controllers.
The scheduler timer is setup (on x86: lapic timer), interrupts are enabled and the kernel enters its second stage: init, but with multitasking.

### Threaded Init

In this stage, the kernel runs as a multi-threaded program. This allows things like device discovery to use time-outs and wait periods without hurting overall system boot times. The majority of code is run here.

The initial tasks run here can be found in `kernel/InitTasks.cpp`, and will fork off into other tasks as time progress.

After all kernel init tasks are done, the kernel runs the program located at `/initdisk/startup`. This can be thought of like the `init` program on linux. It's responsible for setting up userspace, starting the various rpc servers and ultimately providing the appropriate UI to start a user session.

The `/initdisk/` directory is mounted from the kernel module by the name `northport-initdisk`, so this program must be prepared before the system is started. Fortunately its only a tar archive so no fancy bootstrapping tools are needed for this.

### Post Init

Eventually all threaded init tasks will finish, and the kernel will enter the post-init stage. This is where user code is called, and things like a desktop environment will be setup.


## The Major Players (Subsystems)

After the first part of the boot process, the kernel becomes more of a collection of subsystems rather than a single program.
Each is responsible for various things.

The major subsystems currently are:
- Devices (managed by `DeviceManager`). These represent hardware or functionality within the system. Usually these are managed by a driver.
- Drivers (managed by `DriverManager`). Loadable and unloadable code that manages various parts of the system. These can be device drivers, filesystem drivers or anything else.
- Filesystem (managed by `VFS`). Represents the currently mounted filesystens, and the vfs they're mounted to. We use a single-root style VFS.
- PCI (managed by `PciBridge`). Not a lot happens here, but it provides functionality for dealing with pci devices and functions.
- Memory (managed by `PhysicalMemoryManager` and `VirtualMemoryManager`). Two separate levels of code to manage available physical memory, and how that is mapped into a virtual memory space. There is also a helper class (`Paging`), a high level wrapper around a set of page tables.
- Scheduling (managed by `Scheduler`). Northport has a very basic pre-emptive multi-core scheduler. The scheduler maintains a single list of threads, and each core grabs the next available task.
- IPC (managed by `IpcManager`). IPC is mainly implemented as shared memory, either directly or with the kernel doing a double copy via its own buffers. Mailbox IPC is single copied, from the sender's memory into the receivers memory using the kernel hhdm.

There are also a number of generic (coalesced) devices:
- Keyboard: All keyboards forward their input here to be processed.
- Mouse: Same as keyboard.
- SystemClock: An abstraction over the various timers available.

These dont interact directly with hardware, instead the hardware drivers forward their output here, so that it can be processed in a single place. It's just an abstraction layer to make getting input easier.

## Northport Terminology
A collection of non-standard terms used in the project, and their meanings:
- `sumac`: Not the dried berry, but instead it's a platform agnostic term for SMAP (x86) and SUM (rv). Supervisor/User Memory Access Control causes a page fault if supervisor mode code tries to access user mode data. It can be temporarily suspended using the `CPU::AllowSumac()` function.
