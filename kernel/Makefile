CXX_SRCS = Entry.cpp Exit.cpp KernelThread.cpp Panic.cpp \
	core/Clock.cpp core/Config.cpp core/Event.cpp core/WiredHeap.cpp core/IntrRouter.cpp \
	core/Log.cpp core/Pmm.cpp core/RunLevels.cpp core/Scheduler.cpp core/Smp.cpp \
	cpp/Stubs.cpp \
	interfaces/intra/BakedConstants.cpp \
	services/AcpiTables.cpp services/MagicKeys.cpp services/Program.cpp services/Vmm.cpp \
	services/VmPagers.cpp

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/Arch.mk
include interfaces/loader/BootProtocol.mk

LD_SCRIPT = $(ARCH_DIR)/Linker.lds
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
AS_OBJS = $(patsubst %.S, $(BUILD_DIR)/%.S.o, $(AS_SRCS))
OBJS = $(AS_OBJS) $(CXX_OBJS)

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(KERNEL_FULL_FILEPATH)

$(KERNEL_FULL_FILEPATH): $(OBJS) $(LD_SCRIPT)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Linking ...\r\n"
	$(LOUD)$(X_LD_BIN) $(OBJS) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Kernel]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR) $(LOUD_REDIRECT) ||:
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

BAKED_CONSTANTS_FILE = interfaces/intra/BakedConstants.cpp
.PHONY: $(BAKED_CONSTANTS_FILE)
$(BAKED_CONSTANTS_FILE):
	@printf "$(C_BLUE)[Kernel]$(C_RST) Creating source file for build-time kernel constants\r\n"
	@mkdir -p $(@D)
	@printf "#include <interfaces/intra/BakedConstants.h>\n \
		namespace Npk \n\
		{ \n\
			const char* targetArchStr = \"$(CPU_ARCH)\"; \n\
			const char* gitCommitHash = \"$(shell git rev-parse HEAD)\"; \n\
			const char* gitCommitShortHash = \"$(shell git rev-parse --short HEAD)\"; \n\
			size_t versionMajor = 0; \n\
			size_t versionMinor = 5; \n\
			size_t versionRev = 0; \n\
			const char* toolchainUsed = \"$(TOOLCHAIN)\"; \n\
		}" > $(BAKED_CONSTANTS_FILE)

$(BUILD_DIR)/%.S.o: %.S
	@printf "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
