help-text:
	@printf "\e[1;39mUsage: \e[0;39mmake $(C_YELLOW)<target>$(C_RST), where $(C_YELLOW)<target>$(C_RST) is one of the following:\r\n"
	@printf "  $(C_YELLOW)all$(C_RST)\t\t Compiles everything, builds a bootable iso.\r\n"
	@printf "  $(C_YELLOW)clean$(C_RST)\t\t Removes all temporary build files.\r\n"
	@printf "  $(C_YELLOW)run$(C_RST)\t\t Builds everything and launches the kernel in qemu.\r\n"
	@printf "  $(C_YELLOW)run-kvmless$(C_RST)\t Same as $(C_YELLOW)all$(C_RST), but without KVM.\r\n"
	@printf "  $(C_YELLOW)debug$(C_RST)\t\t Builds everything, launches in qemu and waits for a GDB connection.\r\n"
	@printf "  $(C_YELLOW)attach$(C_RST)\t Launches gdb-multiarch and connects to a qemu gdb-server.\r\n"
	@printf "  $(C_YELLOW)docs$(C_RST)\t\t Compiles documentation into pdf format.\r\n"
	@printf "  $(C_YELLOW)docs-clean$(C_RST)\t Removes temporary build files for docs.\r\n"
	@printf "  $(C_YELLOW)cache-clean$(C_RST)\t Removes devel-cache files (local copies of limine, uefi firmware)\r\n\r\n"
	@printf "User-level build settings are found in \e[4;39mConfig.mk\e[0;39m.\r\n"
	@printf "If this is a fresh clone of the repo, please check this file for some first-time setup.\r\n\r\n"
	@printf "\e[1;39mRequirements:\e[0;39m\r\n"
	@printf "Building requires core utils (should be pre-installed) and cross-compiler.\r\n"
	@printf "Both GCC and clang are supported as compilers, see the options in \e[4;39mConfig.mk\e[0;39m.\r\n"
	@printf "To build an iso (recommended) you'll need a copy of the Limine Bootloader and xorriso.\r\n"
	@printf "To use the built-in debug target, gdb-multiarch is required.\r\n"
	@printf "To build the manual a texlive install is required. Pre-build versions can also be found under the releases tab of each mirror.\r\n"
	@printf "For more detailed instructions see \e[4;39mdocs/Building.md\e[0;39m or the dedicated section in the manual.\r\n\r\n"
	@printf "\e[1;39mOnline Mirrors:\e[0;39m\r\n"
	@printf "  Github: https://github.com/deanoburrito/northport\r\n"
	@printf "  Codeberg: https://codeberg.org/r4/northport\r\n"

