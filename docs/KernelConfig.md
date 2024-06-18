# Kernel Config Options

## Command Line Arguments

- `kernel.boot.print_tags`: whether or not to print the contents of the boot protocol responses. This wont disable logging whether the responses were populated or not.
- `kernel.heap.check_bounds`: can enable checking for buffer overruns/underruns within kernel heap. This will use extra memory per-allocation, and has a performance penalty.
- `kernel.heap.trash_after_use`: kernel heap memory is filled with random data after being freed. This incurs a performance penalty to freeing memory but can catch use-after-free bugs.
- `kernel.heap.trash_before_use`: similar to trash_after_use, this helps catch initialization errors with objects allocated on the kernel heap by filling new allocations with random data before returning to the caller.
- `kernel.heap.log_expansion`: emits a log each time the heap expands. This introduces a lot of to the logs, but can be useful in some circumstances.
- `kernel.pmm.trash_before_use`: writes junk data to physical before returning it to the caller, similar usage to the heap feature.
- `kernel.pmm.trash_after_use`: writes junk data to physical memory after freeing it, similar usage to the heap feature.
