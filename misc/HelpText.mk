help-text:
	@echo -e "\e[1;39mUsage: \e[0;39mmake $(C_YELLOW)<target>$(C_RST), where $(C_YELLOW)<target>$(C_RST) is one of the following:"
	@echo -e "  $(C_YELLOW)all$(C_RST)\t\t Compiles everything, builds a bootable iso."
	@echo -e "  $(C_YELLOW)clean$(C_RST)\t\t Removes all temporary build files."
	@echo -e "  $(C_YELLOW)run$(C_RST)\t\t Builds everything and launches the kernel in qemu."
	@echo -e "  $(C_YELLOW)run-kvmless$(C_RST)\t Same as $(C_YELLOW)all$(C_RST), but without KVM."
	@echo -e "  $(C_YELLOW)debug$(C_RST)\t\t Builds everything, launches in qemu and waits for a GDB connection."
	@echo -e "  $(C_YELLOW)attach$(C_RST)\t Launches gdb-multiarch and connects to a qemu gdb-server."
	@echo -e "  $(C_YELLOW)docs$(C_RST)\t\t Compiles documentation into pdf format."
	@echo -e "  $(C_YELLOW)docs-clean$(C_RST)\t Removes temporary build files for docs."
	@echo
	@echo -e "User-level settings are found in \e[4;39mConfig.mk\e[0;39m."
	@echo -e "If this is a fresh clone of the project, this file contains some first-time setup you'll need to perform."
	@echo
	@echo -e "\e[1;39mRequirements:\e[0;39m"
	@echo -e "Building requires core utils (should be pre-installed) and cross-compiler."
	@echo -e "Both GCC and clang are supported as compilers, see the options in \e[4;39mConfig.mk\e[0;39m."
	@echo -e "To build an iso (recommended) you'll need a copy of the Limine Bootloader and xorriso."
	@echo -e "To use the built-in debug target, gdb-multiarch is required."
	@echo -e "To build the documentations, a texlive install is required. Alternatively pre-built versions are \
	on github at \e[4;39mhttps://github.com/DeanoBurrito/northport/releases\e[0;39m."
	@echo
	@echo -e "For more detailed instructions see \e[4;39mdocs/Building.md\e[0;39m or the dedicated section in the manual."

