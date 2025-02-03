# Kernel Config Options

## Command Line Arguments

- `kernel.boot.print_tags`: whether or not to print the contents of the boot protocol responses. This wont disable logging whether the responses were populated or not.
- `kernel.boot.print_cpu_features`: whether or not to dump the supported feature set of the current cpu.
- `kernel.smp.inhibit`: prevents starting additional cores to the BSP. This is from the kernel's perspective, the firmware or bootloader may still bring up additional cores if they are present.
- `kernel.heap.trash_after_use`: kernel heap memory is filled with random data after being freed. This incurs a performance penalty to freeing memory but can catch use-after-free bugs.
- `kernel.heap.trash_before_use`: similar to trash_after_use, this helps catch initialization errors with objects allocated on the kernel heap by filling new allocations with random data before returning to the caller.
- `kernel.pmm.trash_before_use`: writes junk data to physical before returning it to the caller, similar usage to the heap feature.
- `kernel.pmm.trash_after_use`: writes junk data to physical memory after freeing it, similar usage to the heap feature.
- `kernel.vmd.wake_page_count`: sets the threshold value for waking the virtual memory daemon (page-out thread). When this number (or fewer) free pages are left in the PMM, it will wake the thread.
- `kernel.vmd.wake_page_percent`: similar to `wake_page_count`, but offers a percentage-based value (as the total number of usable pages).
- `kenrel.vmd.wake_timeout_ms`: VM daemon will run unconditionally after a number of milliseconds (500ms by default), this allows the value to be overriden.
- `kernel.timer.dump_calibration_data`: dumps raw timer calibration data, not useful on all platforms, but can be helpful for diagnosing time-related issues.
- `kernel.clock.uptime_freq`: overrides the default frequency of the global uptime counter. This only affects the timestamps of logs and not clock event expiry times.
- `kernel.clock.force_sw_uptime`: if set to true, the kernel will always use the software based uptime clock, as opposed to using hardware. This is intended mainly for testing purposes, hardware timers should always be preferrable.
- `kernel.enable_magic_panic_key`: allows using the magic key combo followed by the `p` key to panic the kernel, useful for testing.
- `kernel.enable_magic_shutdown_key`: similar to above, pressing the magic key combo followed by the `s` key will cause the kernel to exit, and attempt to shutdown the system.
- `kernel.enable_all_magic_keys`: enables all the above magic key actions, useful for debugging or in trusted environments.
- `kernel.scheduler.priorities`: allows overriding the number of scheduler priorities used.
- `kernel.log.no_fb_output`: prevent the logging subsystem from spawning terminal emulators on any detected framebuffers, bootloader or runtime.

## Pre-processor Defines

- `NPK_HAS_KERNEL`: defined if code is being compiled as part of the kernel.
- `NPK_HAS_KASAN`: defined (to any value) if the kernel is being compiled with kasan.
