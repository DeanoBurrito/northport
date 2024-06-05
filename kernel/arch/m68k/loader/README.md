# Limine Bootshim - M68k series
**DO NOT USE THIS**

I made this as part of a challenge with another member of the osdev discord server, to see who could port their project to the qemu virt m68k machine first. It's been tested in that single scenario, and likely only on my system. If you're unfamiliar with the limine protocol, its exclusively 64-bit, so I've made some modifications to the spec (see below) in order for it to be usable here.

Having said that, this does what you'd expect: it allows the northport kernel to boot natively (via limine) on the qemu m68k virt board. It provides most of the responses the northport kernel needs, there are some others it doesn't (or can't - dtb, rsdp, efi system table). In theory it should work for any limine-compliant kernel (keeping in mind the changes below). The build makefile in this directly embeds whatever file it finds in `build/kernel.elf` and tries to load it. If qemu is passed a file via `-initrd` that will be made available to the kernel as a module, with a hardcoded command line string.

## Limine Boot Protocol Modifications
Please note: these changes are my own, and not endorsed by the original authors of the limine protocol. If you're using this shim for your own kernel, do not ask for help upstream or bother actual limine devs.

- All pointers are still 64-bits wide, this allows you to use the existing `limine.h` header from the bootloader.
- The kernel is still required to be loaded at -2G or higher, given this is the entire higher half for a 32-bit system the upper 256M is recommended. The bootloader will honour whatever load address the kernel requires in it's PHDRs.
- The kernel **must** be a 32-bit ELF, no support for boot anchors is provided.

### SMP Feature 
### Paging Mode Feature
