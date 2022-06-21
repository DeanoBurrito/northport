# Kernel Command Line
This serves as a listing of all the currently supported kernel command line options. The kernel command line is broken down, and placed in the kernel configuration, after the built in values have been loaded.

## Boot
- `boot_disable_smp`: Tells the kernel to ignore all other cpu processors in the system, and only run on the current (boot) processor.
- `boot_init_program`: Can be used to ask the kernel to load another program instead of the default `/initdisk/apps/startupe.elf`.

## PCI
- `pci_force_legacy_access`: Tells the PCI subsystem to ignore mmio access (ECAM) and only use the legacy port IO. Mainly for testing purposes.

## System Calls
- `syscall_log_requests`: The kernel will emit a log message for every each system call. This only dumps the registers passed to the syscall.
- `syscall_log_responses`: Similar to above, but the registers are dumped after the system call has completed, which shows the values being returned to the user program.

## Logging
- `log_panic_on_error`: By default on logs with the `Fatal` level will cause a panic, enabling this causes `Error` logs to do the same.
