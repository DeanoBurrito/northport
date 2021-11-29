#!/bin/bash
#this script builds a gcc cross compiler and tools for the target platform. Installs them to the default location.
#NOTE: even if this script fails partway, it can serve as a guide of what needs to be installed.

#-- SETTINGS --

# where to install the resulting tools
INSTALL_LOCATION="$HOME/Documents/cross-tools"

#target triplet for binutils/gcc to build
TARGET=x86_64-elf

#download mirror and filenames for binutils/gcc
SOURCE_BINUTILS=https://ftp.gnu.org/gnu/binutils/
FILE_BINUTILS=binutils-2.37
SOURCE_GCC=https://ftp.gnu.org/gnu/gcc/gcc-11.2.0/
FILE_GCC=gcc-11.2.0
FILE_EXTENSIONS=.tar.xz

#-- END SETTINGS --

#first up: lets create a working directory, for easy cleanup later
echo "Welcome to northport build environment setup, to tweak any settings, please edit scripts/create-toolchain.sh"
echo "This script will download and install gcc/binutils for the selected platform."
echo "The setup process will now begin, please note this can take some time depending on your system."

export PATH="$INSTALL_LOCATION/bin:$PATH"
export PREFIX=$INSTALL_LOCATION
export TARGET=$TARGET

mkdir -p $INSTALL_LOCATION
cd $INSTALL_LOCATION
mkdir build-gcc
mkdir build-binutils

#download the required files
echo "Downloading source files ..."
wget $SOURCE_BINUTILS$FILE_BINUTILS$FILE_EXTENSIONS
wget $SOURCE_GCC$FILE_GCC$FILE_EXTENSIONS

#... and extract them (and cleanup downloaded files)
echo "Extracting ..."
tar -xvf $FILE_BINUTILS$FILE_EXTENSIONS
tar -xvf $FILE_GCC$FILE_EXTENSIONS
rm -rf $FILE_BINUTILS$FILE_EXTENSIONS
rm -rf $FILE_GCC$FILE_EXTENSIONS

#now we install some dependencies
echo "Installing dependencies ..."
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo xorriso mtools qemu qemu-system

#setup limine
echo "Cloning limine ..."
cd $INSTALL_LOCATION
git clone https://github.com/limine-bootloader/limine.git --branch=v2.0-branch-binary --depth=1
cd limine
make install

#setup binutils
echo "Building binutils (please note this can take a long time) ..."
cd $INSTALL_LOCATION/build-binutils
../$FILE_BINUTILS/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j $(nproc)
make -j $(nproc) install
cd ..
rm -rf build-binutils

#setup gcc
echo "Building GCC (this can also take a long time) ..."
echo "  Please note libgcc is built with red zone disabled, and with large code model (64bit addressing)."
cd $INSTALL_LOCATION/build-gcc
../$FILE_GCC/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make -j $(nproc) all-gcc
make -j $(nproc) all-target-libgcc CFLAGS_FOR_TARGET='-O2 -mcmodel=large -mno-red-zone'
make -j $(nproc) install-gcc
make -j $(nproc) install-target-libgcc
cd ..
rm -rf build-gcc

#finish
echo "If you changed the default install path, update TOOLCHAIN_DIR in the root makefile to match."
echo "Build environment setup complete!"
