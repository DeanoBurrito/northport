# Northport Documentation

This is the general documentation dump for northport.
The folder structure roughly matches the source tree itself, although it rarely does as deep.
The plan isn't to document everything (although that would be nice), but start with the more complex systems.
Things like PCI, the loadable driver system, USB and some other protocol stacks.

All documentation is written in standard markdown.

# Project Contributors
While git and github track commits, it's nice to put this in some solid text within the project.
Here's the list of contributors (so far), and in no particular order:

- [DeanoBurrito](https://github.com/DeanoBurrito).
- [Ivan G](https://github.com/dreamos82).

# Useful OS Dev Resources
Outside the scope of this project, but a collection of things I've found useful during the course of this project.

- *[Intel Software Development Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)*. This one is precise, and gives you ever detail you'll ever need.
- *[AMD Architecture Programmers Manual](https://developer.amd.com/resources/developer-guides-manuals/)*. Less detailed, but much easier to read, and locate information you're unfamiliar with. Great for understanding new topics.
- *[The OS Dev Wiki](https://wiki.osdev.org/Expanded_Main_Page)*. Quite dated in areas, but great for high level overviews and getting started in general.
- *Operating Systems Development Discord Server*. Wont like to avoid attracting spam from web scrapers, great place to for realtime Q/A.
- *The ELF format specification, ELF64 and System V ABI platform extensions*. Great reference point for all things ABI, parsing kernel symbol tables and loading your own ELFs.
- *[Agner Fog - Calling Conventions](https://www.agner.org/optimize/calling_conventions.pdf)*. Comparison and light documenation of calling conventions, C++ name mangling, executable formats and more. 
