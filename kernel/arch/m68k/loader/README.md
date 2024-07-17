# Limine Bootshim - M68k series
I made this as part of a challenge with another member of the osdev discord server, to see who could port their project to the qemu virt m68k machine first. It's been tested in that single scenario, and likely only on my system. If you're unfamiliar with the limine protocol, its intended to be exclusively 64-bit, so I've made some modifications to the spec (see below) in order for it to be usable here.

Having said that, this does what you'd expect: it allows the northport kernel to boot natively (via limine) on the qemu m68k virt board. It provides most of the responses the northport kernel needs, and there are some that it should provide but doesnt right now. Naturally there are some requests it cant repond to (dtb, rsdp, efi runtime services table). In theory this should work with any limine-compliant kernel, keeping in mind the changes below.

## Bootloader Config

The shim is extremely primitive, but it does allow some configuration at compile-time. The following macros can be defined when compiling the shim:
- `NPL_INITRD_CMDLINE`: The shim can only provide a single module to the kernel (passed via the `-initrd` arg to qemu), this macro can be used to set the cmdline field for the module.
- `NPL_KERNEL_CMDLINE`: If defined, this macro is used as a string to append to the kernel command line, passed as the cmdline field of the kernel file response. If the firmware also passes a command line to the loader (for example, via qemu's `-append` flag) these strings are concatenated and the result is passed to the kernel.

## Limine Boot Protocol Modifications
Please note: these changes are my own, and not endorsed by the original authors of the limine protocol. If you're using this shim for your own kernel, do not ask for help upstream or bother actual limine devs.

- All pointers are natively sized: 64-bits on 64-bit systems (existing targets of the limine protocol), and 32-bits on 32-bit systems (m68k). While this could have remain unchanged, it's more convinient for the kernel and loader this way.
- The kernel is still required to be loaded in the upper 2GiB of virtual memory, however it's recommended the kernel is loaded higher than, as this is the entire higher half on a 32-bit system.
- The kernel **must** be a 32-bit ELF, no support for boot anchors is provided.

- The transparent translation registers are cleared to zero (disabled).

### SMP Feature 

### Paging Mode Feature
The following values are added for the m68k implementation:

```c
#define LIMINE_PAGING_MODE_M68K_4K 0
#define LIMINE_PAGING_MODE_M68k_8K 1

#define LIMINE_PAGING_MODE_DEFAULT LIMINE_PAGING_MODE_M68K_4K
```

No flags are currently defined. The default (if no request is present) is `LIMINE_PAGING_MODE_M68K_4K`.
