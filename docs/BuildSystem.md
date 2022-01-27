# Northport Build System

Northport is built using GNU makefiles (we use some GNU-specific extensions), so it may or may not work with other *make variants.
It follows a recursive style, where the top level makefile does a bunch of config, and then calls each project's own makefile.
That project makefile is responsible for honouring things like using the set build directory, output and flags.

## High Level Overview

```
northport/
    | - initdisk/
    |   \ - Makefile
    |
    | - kernel/
    |   \ - Makefile
    |
    | - libs/
    |   | - Makefile
    |   |
    |   | - np-syslib
    |   |   \ - Makefile
    |   |
    |   | - np-graphics
    |   |   \ - Makefile
    [ ... other northport libs here]
    |
    | - misc/
    |   \ - LibCommon.mk
    |
    | - BuildPrep.mk
    | - Run.mk
    \ - Makefile
```

Starting bottom up: the root makefile is a glorified config file and dispatcher really.
It'll trigger the initdisk, library and kernel to build/clean themselves with the appropriate options.

### BuildPrep.mk
This file is really just creating the appropriate preprocessor defines to represent the build options chosen in the root makefile. It's separated out here as it's only touched once when a new feature is added. Otherwise it just adds noise to the main file.

### Run.mk
Separation of concerns here. This file contains qemu and gdb stuff, its not related to the core build system so I wanted to get it out of the root makefile.

### misc/LibCommon.mk
Contains a default `make clean` implementation.
Also has a rule for building object files from cpp ones.
It assumes headers are stored in `include/` inside the project dir.

It requires the following variables to work properly:
- `CXX_FLAGS`: any c++ compilers flags you want to use. You'll need to include `CXX_GLOBAL_FLAGS` here yourself.
 - `CXX_SRCS`: any c++ source files to compile.
- `TARGET`: the name of the project. This is used for output diagnostics, and for copying the include files to the correct destination. No file extension should be used.

## Adding a library
Adding a library to the build system is pretty straight forward:
1. First copy `libs/np-barebones` into a folder of your choice.
2. Second add your library to `libs/Makefile` under the `SUB_PROJS` rule.
4. Profit!

Now you library will be included in the default builds of northport.
