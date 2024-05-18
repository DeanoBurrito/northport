# Kernel Config Options

## Command Line Arguments

- `kernel.boot.print_tags`: whether or not to print the contents of the boot protocol responses. This wont disable logging whether the responses were populated or not.
- `kernel.heap.check_bounds`: can enable checking for buffer overruns/underruns within kernel heap. This will use extra memory per-allocation, and has a performance penalty.
- `kernel.heap.trash_after_use`: kernel heap memory is filled with random data after being freed. This incurs a performance penalty to freeing memory but can catch use-after-free bugs.
- `kernel.heap.trash_before_use`: similar to trash_after_use, this helps catch initialization errors with objects allocated on the kernel heap by filling new allocations with random data before returning to the caller.
