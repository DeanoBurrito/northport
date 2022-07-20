# Northport Documentation

Northport's main documentation can be found [here](https://github.com/deanoburrito/northport-docs), these files are just an in-tree dumping ground for various resources related to the main readme. 

# Project Contributors
While git and github track commits, it's nice to put this in some solid text within the project.
Here's the list of contributors (so far), and in no particular order:

- [Dean T (DeanoBurrito)](https://github.com/DeanoBurrito).
- [Ivan G (dreamos82)](https://github.com/dreamos82).

# Useful OS Dev Resources
Outside the scope of this project, but a collection of things I've found useful during the course of this project.

- *[Intel Software Development Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)*. This one is precise, and gives you more detail than you'll ever need.
- *[AMD Architecture Programmers Manual](https://developer.amd.com/resources/developer-guides-manuals/)*. Less detailed, but much easier to read, and locate information you're unfamiliar with. Great for understanding new topics.
- *[The OS Dev Wiki](https://wiki.osdev.org/Expanded_Main_Page)*. Quite dated in areas, but great for high level overviews and getting started in general.
- *Operating Systems Development Discord Server*. Wont link to avoid attracting spam from web scrapers, great place to for realtime Q/A.
- *The ELF format specification, ELF64 and System V ABI platform extensions*. Great reference point for all things ABI, parsing kernel symbol tables and loading your own ELFs.
- *[Agner Fog - Calling Conventions](https://www.agner.org/optimize/calling_conventions.pdf)*. Comparison and light documenation of calling conventions, C++ name mangling, executable formats and more. 

# Unrelated Resources
- *[qoi](https://github.com/phoboslab/qoi)*. The 'Quite Okay Image' format, an extremely simple alternative to PNG. Check the official website for more details, used as the native image format due to a number of reasons (simple implementatin mainly, but the spec is public domain too which is nice).
- *[GDB Remote Server Protocol Spec](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html)*. Describes everything required to implement a gdbserver that can response to `target remote` connections. *[This](https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html)* is an excellent breakdown of how to write a gdb server stub.
