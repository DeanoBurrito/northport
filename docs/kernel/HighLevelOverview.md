# High Level Overview 

## Boot Sequence

The kernel currently has 3 main states during its lifetime. 

### Basic Init

The kernel takes control of the machine from the bootloader and sets up a physical memory manager. There is only one per system, so this is the first thing to be started.
After this an initial set off page tables is constructed. These are modelled after the stivale2 boot protocol.
- A hhdm (**h**igher **h**alf **d**irect **m**ap of physical memory in virtual space) address is decided. This is usually the lowest possible address in the higher half of the virtual address space. If booted via stivale2, we use the provided hhdm address. This changes whether 4 or 5 level paging is used.
- The first 4GB of physical memory is mapped at the hhdm.
- Any memory regions marked as 'usable', and above 4GB, are mapped into the hhdm, this includes most of ram.
- The entire kernel blob is mapped at 0xffff'ffff'8000'0000. 

Then the kernel heap is initialized, and so is formatted log output (since we can dynamically allocate now).

Next the kernel core is setup, things like an interrupt controller and its routing (idt on x86). Other platform specific stuff goes on here, like finding/parsing ACPI tables. The scheduler data is also initialized here, although it isnt started until after the other cores have joined the kernel in long mode.

The finish the first stage of the kernel init, all the other cpu cores are started and progress to 64-bit long mode. Then all cores setup their core local data structures (on x86: tss, gdt. Idt is actually shared between all cores), and respective interrupt controllers.
The scheduler timer is setup (on x86: lapic timer), interrupts are enabled and the kernel enters its second stage: init, but with multitasking.

### Threaded Init

In this stage, the kernel runs as a multi-threaded program. This allows things like device discovery to use time-outs and wait periods without hurting overall system boot times. The majority of code is run here.

The initial tasks run here can be found in `kernel/InitTasks.cpp`, and will fork off into other tasks as time progress.

### Post Init

Eventually all threaded init tasks will finish, and the kernel will enter the post-init stage. This is where user code is called, and things like a desktop environment will be setup.


## The Major Players (Subsystems)

After the first part of the boot process, the kernel becomes more of a collection of subsystems rather than a single program.
Each is responsible for various things.

The major subsystems currently are:
- Devices (managed by DeviceManager). These represent hardware or functionality within the system. Usually these are managed by a driver.
- Drivers (managed by DriverManager). Loadable and unloadable code that manages various parts of the system. These can be device drivers, filesystem drivers or anything else.
- Filesystem (managed by VFS). Represents the currently mounted filesystens, and the vfs they're mounted to. 
- PCI (managed by PciBridge). Not a lot happens here, but it provides functionality for dealing with pci devices and functions.
- Memory (managed by PhysicalMemoryManager and Paging). Two separate levels of code to managed available physical memory, and how that is mapped into a virtual memory space.
- Scheduling (managed by Scheduler). Northport has a very basic pre-emptive scheduler.

There are also a number of generic (coalesced) devices:
- Keyboard: All keyboards forward their input here to be processed.
- Mouse: Same as keyboard.
- SystemClock: An abstraction over the various timers available.

These dont interact directly with hardware, instead the hardware drivers forward their output here, so that it can be processed in a single place. It's just an abstraction layer to make getting input easier.

These might be folded into the driver subsystem later on.
